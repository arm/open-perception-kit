/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#include "gst/gstelement.h"
#include "gst/gstpad.h"
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <exception>
#include <fmt/core.h>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <variant>

#include "glib-object.h"
#include "glib.h"

#include "Log.h"
#include "pek/Perception.h"
#include "pek/Result.h"
#include "pek/Tools.h"

#include "op/OpChain.h"
#include "op/OpChainContext.h"
#include "op/OpChainDescriptor.h"

#include "gst/PerceptionMeta.h"
#include "mediaio/GstVideoFrame.h"
#include "perf/PerformanceTracer.h"

class GstPekInferMembers {
  public:
    pek::Result<void> loadOpChainDescriptor(const std::string &filePath) {
        auto descriptorResult = pek::op::OpChainDescriptor::fromFile(filePath);
        if (!descriptorResult)
            return tl::unexpected{descriptorResult.error()};
        descriptor = std::move(*descriptorResult);
        return {};
    }

    [[nodiscard]] const std::string &modelName() const noexcept {
        return descriptor.name;
    }

    pek::op::OpChain *opChainForActiveFrame() {
        pek::op::OpChain *readyOpChain = nullptr;
        {
            std::lock_guard lock(setupMutex);
            if (setupState == Idle) {
                setupFailure.clear();
                setupFailureReported = false;
                retryAfterDeactivation = false;
                try {
                    setupWorker = std::jthread(
                        [this](std::stop_token stopToken) { setupAttempt(stopToken); });
                    setupState = Loading;
                } catch (const std::exception &error) { // NOSONAR: protect the jthread boundary.
                    setupFailure = fmt::format("Could not start setup worker: {}", error.what());
                    setupState = Failed;
                } catch (...) { // NOSONAR: preserve the asynchronous failure boundary.
                    setupFailure = "Could not start setup worker: unknown error";
                    setupState = Failed;
                }
            } else if (setupState == Ready) {
                readyOpChain = opChain.get();
            }
        }
        return readyOpChain;
    }

    std::optional<std::string> takeSetupFailure() {
        std::lock_guard lock(setupMutex);
        if (setupState != Failed || setupFailureReported)
            return std::nullopt;
        setupFailureReported = true;
        return setupFailure;
    }

    void modelDeactivated() {
        std::lock_guard lock(setupMutex);
        if (setupState == Loading) {
            retryAfterDeactivation = true;
        } else if (setupState == Failed) {
            setupState = Idle;
            setupFailure.clear();
            setupFailureReported = false;
        }
    }

  private:
    enum class SetupState { Idle, Loading, Ready, Failed };
    using enum SetupState;

    void setupAttempt(const std::stop_token &stopToken) {
        std::unique_ptr<pek::op::OpChain> candidate;
        std::optional<std::string> failure;
        try {
            candidate = std::make_unique<pek::op::OpChain>();
            if (auto setupResult = candidate->setupFromDescriptor(descriptor, stopToken);
                !setupResult)
                failure = setupResult.error().toString();
        } catch (const std::exception &error) { // NOSONAR: plugin thread exception boundary.
            failure = fmt::format("Unhandled setup exception: {}", error.what());
        } catch (...) { // NOSONAR: external plugins may throw non-standard exceptions.
            failure = "Unhandled non-standard setup exception";
        }

        {
            std::lock_guard lock(setupMutex);
            if (stopToken.stop_requested())
                return;
            if (failure) {
                setupFailure = *failure;
                setupState = retryAfterDeactivation ? Idle : Failed;
            } else {
                opChain = std::move(candidate);
                setupState = Ready;
                retryAfterDeactivation = false;
            }
        }

        if (failure) {
            pek::log::error(
                "Asynchronous OpChain setup failed for [{}]: {}\n", descriptor.name, *failure);
        }
    }

    pek::op::OpChainDescriptor descriptor;
    std::mutex setupMutex;
    SetupState setupState = Idle;
    std::unique_ptr<pek::op::OpChain> opChain;
    std::string setupFailure;
    bool setupFailureReported = false;
    bool retryAfterDeactivation = false;

    // Keep this member last: jthread teardown must stop and join the worker before
    // any state captured through `this` is destroyed.
    std::jthread setupWorker;
};

#ifndef PACKAGE
#define PACKAGE "pek-elements"
#endif

G_BEGIN_DECLS

#define GST_TYPE_PEKINFER (gst_pekinfer_get_type())
G_DECLARE_FINAL_TYPE(GstPekInfer, gst_pekinfer, GST, PEKINFER, GstBaseTransform)

struct _GstPekInfer {
    GstBaseTransform parent;

    // cached from caps
    GstVideoInfo vinfo;

    // Properties
    gchar *opChainPath;
    gboolean active;
    gchar *inferId;

    // a safe place for c++ stuff
    GstPekInferMembers *m;
};

G_END_DECLS

G_DEFINE_TYPE(GstPekInfer, gst_pekinfer, GST_TYPE_BASE_TRANSFORM)

// ---------------- GstBaseTransform virtuals ----------------
//
static const gchar *gst_pekinfer_get_effective_inferId(GstPekInfer *self) {
    /* If user provided infer-id property, prefer it */
    if (self->inferId && self->inferId[0] != '\0')
        return self->inferId;

    /* Fallback to element name (always exists) */
    return GST_OBJECT_NAME(GST_ELEMENT(self));
}

static gboolean gst_pekinfer_is_active(GstPekInfer *self) {
    GST_OBJECT_LOCK(self);
    const gboolean active = self->active;
    GST_OBJECT_UNLOCK(self);
    return active;
}

static gboolean gst_pekinfer_start(GstBaseTransform *b) {
    auto *self = (GstPekInfer *)b;
    static pek::perf::PerformanceTracer *tracer = pek::perf::getGlobalTracer();
    (void)tracer;

    auto members = std::make_unique<GstPekInferMembers>();

    if (!self->opChainPath || !self->opChainPath[0]) {
        GST_ERROR_OBJECT(self, "opchain property is mandatory but not set");
        return FALSE;
    }

    if (auto descriptorResult = members->loadOpChainDescriptor(self->opChainPath);
        !descriptorResult) {
        pek::log::error("Error while loading op-chain descriptor [{}]: {}\n",
                        self->opChainPath,
                        descriptorResult.error().toString());

        GST_ELEMENT_ERROR(
            self, RESOURCE, FAILED, ("Failed to load op-chain."), ("%s", self->opChainPath));

        return FALSE;
    }

    const std::string modelName = members->modelName();
    GST_OBJECT_LOCK(self);
    self->m = members.release();
    GST_OBJECT_UNLOCK(self);

    // Send model registration event downstream
    GstPad *srcpad = gst_element_get_static_pad(GST_ELEMENT(self), "src");
    if (srcpad) {
        GstStructure *structure = gst_structure_new("pek-model-register",
                                                    "model-name",
                                                    G_TYPE_STRING,
                                                    modelName.c_str(),
                                                    "element-name",
                                                    G_TYPE_STRING,
                                                    GST_OBJECT_NAME(self),
                                                    "active",
                                                    G_TYPE_BOOLEAN,
                                                    gst_pekinfer_is_active(self),
                                                    NULL);
        GstEvent *event = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, structure);
        gst_pad_push_event(srcpad, event);
        gst_object_unref(srcpad);
    }

    return TRUE;
}

static gboolean gst_pekinfer_stop(GstBaseTransform *b) {
    auto *self = (GstPekInfer *)b;
    std::unique_ptr<GstPekInferMembers> members;
    GST_OBJECT_LOCK(self);
    members.reset(std::exchange(self->m, nullptr));
    GST_OBJECT_UNLOCK(self);
    return TRUE;
}

static gboolean gst_pekinfer_set_caps(GstBaseTransform *b, GstCaps *incaps, GstCaps *outcaps) {
    auto *self = (GstPekInfer *)b;
    (void)outcaps;

    if (!gst_video_info_from_caps(&self->vinfo, incaps)) {
        GST_ERROR_OBJECT(self, "Failed to parse input caps");
        return FALSE;
    }

    // Keep original assumption: RGB only
    if (GST_VIDEO_INFO_FORMAT(&self->vinfo) != GST_VIDEO_FORMAT_BGRA) {
        GST_ERROR_OBJECT(self, "Unsupported format (expected BGRA)");
        return FALSE;
    }

    return TRUE;
}

static GstFlowReturn gst_pekinfer_transform_ip(GstBaseTransform *b, GstBuffer *buf) {
    auto *self = (GstPekInfer *)b;

    if (!self->m)
        return GST_FLOW_OK;

    if (!gst_pekinfer_is_active(self))
        return GST_FLOW_OK;

    auto *opChain = self->m->opChainForActiveFrame();
    if (!opChain) {
        if (auto failure = self->m->takeSetupFailure()) {
            GST_ELEMENT_WARNING(self,
                                RESOURCE,
                                FAILED,
                                ("Asynchronous OpChain setup failed; inference remains "
                                 "pass-through."),
                                ("%s", failure->c_str()));
        }
        return GST_FLOW_OK;
    }

    // Try to get the perception meta
    // it does not added yet -> add it
    if (auto perceptionMeta = pek::PerceptionMeta::get(buf); !perceptionMeta) {
        auto perception = std::make_shared<pek::Perception>();
        if (!pek::PerceptionMeta::add(buf, perception)) {
            GST_WARNING_OBJECT(self, "Failed to attach PerceptionMeta");
            return GST_FLOW_OK;
        }
    }

    // Build the pipeline VideoFrame only from CPU-direct buffers for now. DMA-BUF-backed
    // buffers are detected explicitly so future DMA-BUF support can be added without
    // accidentally taking a slow or invalid CPU mapping path.
    std::shared_ptr<pek::mediaio::VideoFrame> sharedMediaFrame;
    if (pek::mediaio::gst::GstVideoFrame::hasDirectCpuAddress(buf)) {
        sharedMediaFrame =
            pek::mediaio::gst::GstVideoFrame::mapGstBuffer(buf, self->vinfo, pek::AccessMode::Read);
        if (!sharedMediaFrame) {
            GST_ELEMENT_ERROR(
                self, RESOURCE, FAILED, ("Failed to map video buffer."), ("%s", self->opChainPath));
            return GST_FLOW_ERROR;
        }
    } else if (pek::mediaio::gst::GstVideoFrame::hasDmaBufContent(buf)) {
        GST_ELEMENT_ERROR(self,
                          RESOURCE,
                          FAILED,
                          ("DMA-BUF video buffers are not supported by pekinfer yet."),
                          ("%s", self->opChainPath));
        return GST_FLOW_ERROR;
    } else {
        GST_ELEMENT_ERROR(self,
                          RESOURCE,
                          FAILED,
                          ("Unsupported GstBuffer memory type."),
                          ("%s", self->opChainPath));
        return GST_FLOW_ERROR;
    }

    // Mutate the PerceptionMeta while executing the op-chain. The mapped frame is
    // passed through the op context and stays alive for the whole op-chain execution.
    auto ret = pek::PerceptionMeta::mutate<GstFlowReturn>(
        buf, [self, sharedMediaFrame, opChain](auto &perception) {
            // Execute the op-chain with the provided context. The chain can read and mutate the
            // perception and read the video frame, but not mutate it.
            pek::op::OpChainContext opChainContext;
            opChainContext.inferenceInfo.inferElementId =
                std::string(gst_pekinfer_get_effective_inferId(self));

            opChainContext.perception = &perception;
            opChainContext.videoFrames["pipelineVideoFrame"] = sharedMediaFrame;

            if (auto executeResult = opChain->execute(opChainContext); !executeResult) {
                pek::log::error("{}\n", executeResult.error().toString());
                return GST_FLOW_CUSTOM_ERROR;
            }

            return GST_FLOW_OK;
        });

    // Collapse op-chain failures and metadata mutation failures into a GStreamer
    // element error so the pipeline fails consistently.
    using ME = pek::MetaError;
    if ((std::holds_alternative<GstFlowReturn>(ret) &&
         std::get<GstFlowReturn>(ret) != GST_FLOW_OK) ||
        std::holds_alternative<ME>(ret)) {
        GST_ELEMENT_ERROR(
            self, RESOURCE, FAILED, ("Error while executing op-chain."), ("%s", self->opChainPath));

        return GST_FLOW_ERROR;
    }

    return GST_FLOW_OK;
}

// ---------------- properties & class init ----------------

namespace {

enum class PropertyId : guint { None, OpChainPath, ModelActive, InferId };
using enum PropertyId;

constexpr guint propertyId(PropertyId id) noexcept {
    return static_cast<guint>(id);
}

} // namespace

static void gst_pekinfer_set_property(GObject *o, guint id, const GValue *v, GParamSpec *ps) {
    auto *self = (GstPekInfer *)o;
    switch (static_cast<PropertyId>(id)) {
    case OpChainPath:
        g_free(self->opChainPath);
        self->opChainPath = g_value_dup_string(v);
        break;
    case ModelActive: {
        GST_OBJECT_LOCK(self);
        self->active = g_value_get_boolean(v);
        if (!self->active && self->m)
            self->m->modelDeactivated();
        GST_OBJECT_UNLOCK(self);
        break;
    }
    case InferId:
        g_free(self->inferId);
        self->inferId = g_value_dup_string(v);
        break;
    case None:
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void gst_pekinfer_get_property(GObject *o, guint id, GValue *v, GParamSpec *ps) {
    auto *self = (GstPekInfer *)o;
    switch (static_cast<PropertyId>(id)) {
    case OpChainPath:
        g_value_set_string(v, self->opChainPath);
        break;
    case ModelActive:
        g_value_set_boolean(v, gst_pekinfer_is_active(self));
        break;
    case InferId:
        g_value_set_string(v, gst_pekinfer_get_effective_inferId(self));
        break;
    case None:
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void gst_pekinfer_finalize(GObject *object) {
    auto *self = reinterpret_cast<GstPekInfer *>(object);

    g_free(self->opChainPath);
    self->opChainPath = nullptr;

    g_free(self->inferId);
    self->inferId = nullptr;

    G_OBJECT_CLASS(gst_pekinfer_parent_class)->finalize(object);
}

static void gst_pekinfer_class_init(GstPekInferClass *klass) {
    GObjectClass *gobj = G_OBJECT_CLASS(klass);
    GstElementClass *ecls = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *bcls = GST_BASE_TRANSFORM_CLASS(klass);

    gobj->set_property = gst_pekinfer_set_property;
    gobj->get_property = gst_pekinfer_get_property;
    gobj->finalize = gst_pekinfer_finalize;

    g_object_class_install_property(
        gobj,
        propertyId(OpChainPath),
        g_param_spec_string("opchain-path",
                            "OpChain path",
                            "Path to OpChain setup JSON",
                            nullptr,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        propertyId(ModelActive),
        g_param_spec_boolean("active",
                             "Active",
                             "Do or not do",
                             true,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        propertyId(InferId),
        g_param_spec_string("infer-id",
                            "ID of the inference element",
                            "ID of the inference element (used in Plumber to identify the layers. "
                            "Defaults to the name property of the element)",
                            "",
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    // Static pad templates (portable across GStreamer-1.0 versions)
    static GstStaticPadTemplate sink_t = GST_STATIC_PAD_TEMPLATE(
        "sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw, format={BGRA}"));
    static GstStaticPadTemplate src_t = GST_STATIC_PAD_TEMPLATE(
        "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw, format={BGRA}"));
    gst_element_class_add_static_pad_template(ecls, &sink_t);
    gst_element_class_add_static_pad_template(ecls, &src_t);

    gst_element_class_set_static_metadata(ecls,
                                          "PEK Inference",
                                          "Filter/Effect/Video",
                                          "ONNX Runtime inference",
                                          "You <you@example.com>");

    bcls->start = gst_pekinfer_start;
    bcls->stop = gst_pekinfer_stop;
    bcls->set_caps = gst_pekinfer_set_caps;
    bcls->transform_ip = gst_pekinfer_transform_ip;
}

static void gst_pekinfer_init(GstPekInfer *self) {
    self->opChainPath = nullptr;
    self->active = true;
    self->m = nullptr;
    self->inferId = nullptr;

    gst_video_info_init(&self->vinfo);

    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(self), FALSE);
    gst_base_transform_set_qos_enabled(GST_BASE_TRANSFORM(self), FALSE);
}

static gboolean plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "pekinfer", GST_RANK_NONE, GST_TYPE_PEKINFER);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  pekinfer,
                  "PEK inference (YOLO + ONNX Runtime)",
                  plugin_init,
                  "1.0",
                  "LGPL",
                  "pek-elements",
                  "https://example.com")
