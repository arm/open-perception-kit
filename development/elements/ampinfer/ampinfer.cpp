#include <filesystem>
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <fmt/core.h>
#include <memory>
// #include <onnxruntime_cxx_api.h>

#include "amp/BitmapView.h"
#include "amp/Perception.h"
#include "amp/PerceptionContext.h"
#include "amp/Types.h"
#include "glib-object.h"
#include "glib.h"
#include "gst/gstpad.h"

#include "nlohmann/json.hpp"
// #include "onnx/Inference.h"

#include "amp/AttributeMap.h"
#include "amp/File.h"
#include "amp/Labels.h"
#include "amp/Result.h"
#include "amp/String.h"
#include "amp/Tools.h"

#include "op/Op.h"
#include "op/OpChain.h"
#include "op/OpChainContext.h"

#include "gst/PerceptionContextMeta.h"
#include <PerformanceTracer.h>

struct GstAmpInferMembers {
    // std::shared_ptr<onnx::Inference> onnxInference;

    amp::OpChain opChain;

    amp::Result<void> executeOpChain(amp::OpChainContext &opChainContext) {
        return opChain.execute(opChainContext);
    }

    amp::Result<void> setupOpChainFromJson(const std::string &filePath) {
        auto setupResult = opChain.setupFromFile(filePath);
        if (!setupResult) {
            return setupResult;
        }
        return {};
    }
};

#ifndef PACKAGE
#define PACKAGE "amp-elements"
#endif

G_BEGIN_DECLS

#define GST_TYPE_AMPINFER (gst_ampinfer_get_type())
G_DECLARE_FINAL_TYPE(GstAmpInfer, gst_ampinfer, GST, AMPINFER, GstBaseTransform)

struct _GstAmpInfer {
    GstBaseTransform parent;

    // cached from caps
    GstVideoInfo vinfo;

    // Properties
    gchar *opChainPath;
    gboolean active;
    gchar *format;

    // a safe place for c++ stuff
    GstAmpInferMembers *m;
};

G_END_DECLS

G_DEFINE_TYPE(GstAmpInfer, gst_ampinfer, GST_TYPE_BASE_TRANSFORM)

// ---------------- GstBaseTransform virtuals ----------------
//
namespace fs = std::filesystem;

static std::optional<fs::path> parent_dir_name(const fs::path &p) {
    if (!p.has_filename()) {
        return std::nullopt;
    }

    fs::path parent = p.parent_path();
    if (parent.empty()) {
        return std::nullopt;
    }

    return parent.filename();
}

static gboolean gst_ampinfer_start(GstBaseTransform *b) {
    auto *self = (GstAmpInfer *)b;
    static amp::PerformanceTracer *tracer = amp::getGlobalTracer();
    (void)tracer;

    self->m = new GstAmpInferMembers();

    if (!self->opChainPath || !self->opChainPath[0]) {
        GST_ERROR_OBJECT(self, "opchain property is mandatory but not set");
        return FALSE;
    }

    auto setupResult = self->m->setupOpChainFromJson(self->opChainPath);
    if (!setupResult) {
        fmt::print("Error while setting up op-chain: {}\n", setupResult.error().toString());
        AMP_ABORT;
    }

    // Send model registration event downstream
    GstPad *srcpad = gst_element_get_static_pad(GST_ELEMENT(self), "src");
    if (srcpad) {
        std::string name = "unknown";
        if (auto dir = parent_dir_name(self->opChainPath); dir.has_value()) {
            name = dir->string();
        }

        GstStructure *structure = gst_structure_new("amp-model-register",
                                                    "model-name",
                                                    G_TYPE_STRING,
                                                    name.c_str(),
                                                    "element-name",
                                                    G_TYPE_STRING,
                                                    GST_OBJECT_NAME(self),
                                                    "active",
                                                    G_TYPE_BOOLEAN,
                                                    self->active,
                                                    NULL);
        GstEvent *event = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, structure);
        gst_pad_push_event(srcpad, event);
        gst_object_unref(srcpad);
    }

    return TRUE;
}

static gboolean gst_ampinfer_stop(GstBaseTransform *b) {
    auto *self = (GstAmpInfer *)b;
    delete self->m;
    self->m = nullptr;
    return TRUE;
}

static gboolean gst_ampinfer_set_caps(GstBaseTransform *b, GstCaps *incaps, GstCaps *outcaps) {
    auto *self = (GstAmpInfer *)b;
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

static GstFlowReturn gst_ampinfer_transform_ip(GstBaseTransform *b, GstBuffer *buf) {
    auto *self = (GstAmpInfer *)b;

    if (!self->active)
        return GST_FLOW_OK;

    if (!self->m)
        return GST_FLOW_OK;

    GstMapInfo map;
    if (!gst_buffer_map(buf, &map, GST_MAP_READWRITE)) {
        GST_WARNING_OBJECT(self, "Failed to map buffer");
        return GST_FLOW_OK;
    }
    const size_t frameWidth = GST_VIDEO_INFO_WIDTH(&self->vinfo);
    const size_t frameHeight = GST_VIDEO_INFO_HEIGHT(&self->vinfo);

    // NOTE: This assumes tightly packed RGB: stride == width*3.
    // If you ever get padded stride, you'll need to map as GstVideoFrame instead.
    uint8_t *rgb = (uint8_t *)map.data;
    if (!rgb) {
        gst_buffer_unmap(buf, &map);
        return GST_FLOW_OK;
    }

    // ======================================================================================

    std::shared_ptr<amp::PerceptionContextMeta> perceptionContextMeta;
    GstBuffer *writable_buf = gst_buffer_make_writable(buf);
    perceptionContextMeta = amp::PerceptionContextMeta::get(writable_buf);
    if (!perceptionContextMeta) {
        perceptionContextMeta =
            amp::PerceptionContextMeta::attach(writable_buf, new amp::Perception());
    }
    auto perceptionContext_ptr = perceptionContextMeta->get_payload();

    amp::OpChainContext opChainContext;

    amp::BitmapView pipelineFrame(rgb, amp::DataKind::ImageBgraHwc, frameWidth, frameHeight);

    opChainContext.perception = perceptionContext_ptr;
    opChainContext.bitmapViews["pipelineVideoFrame"] = pipelineFrame;

    auto executeResult = self->m->executeOpChain(opChainContext);
    if (!executeResult) {
        fmt::print("{}\n", executeResult.error().toString());
        gst_buffer_unmap(buf, &map);
        AMP_ABORT;
        return GST_FLOW_OK;
    }

    return GST_FLOW_OK;
}

// ---------------- properties & class init ----------------

enum { PROP_0, PROP_OPCHAIN_PATH, PROP_MODEL_ACTIVE, PROP_FORMAT };

static void gst_ampinfer_set_property(GObject *o, guint id, const GValue *v, GParamSpec *ps) {
    auto *self = (GstAmpInfer *)o;
    switch (id) {
    case PROP_OPCHAIN_PATH:
        g_free(self->opChainPath);
        self->opChainPath = g_value_dup_string(v);
        break;
    case PROP_MODEL_ACTIVE: {
        self->active = g_value_get_boolean(v);
        break;
    }
    case PROP_FORMAT:
        g_free(self->format);
        self->format = g_value_dup_string(v);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void gst_ampinfer_get_property(GObject *o, guint id, GValue *v, GParamSpec *ps) {
    auto *self = (GstAmpInfer *)o;
    switch (id) {
    case PROP_OPCHAIN_PATH:
        g_value_set_string(v, self->opChainPath);
        break;
    case PROP_MODEL_ACTIVE:
        g_value_set_boolean(v, self->active);
        break;
    case PROP_FORMAT:
        g_value_set_string(v, self->format);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void gst_ampinfer_class_init(GstAmpInferClass *klass) {
    GObjectClass *gobj = G_OBJECT_CLASS(klass);
    GstElementClass *ecls = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *bcls = GST_BASE_TRANSFORM_CLASS(klass);

    gobj->set_property = gst_ampinfer_set_property;
    gobj->get_property = gst_ampinfer_get_property;

    g_object_class_install_property(
        gobj,
        PROP_OPCHAIN_PATH,
        g_param_spec_string("opchain-path",
                            "OpChain path",
                            "Path to OpChain setup JSON",
                            nullptr,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_MODEL_ACTIVE,
        g_param_spec_boolean("active",
                             "Active",
                             "Do or not do",
                             true,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_FORMAT,
        g_param_spec_string("format",
                            "Video format",
                            "Video format (BGRA)",
                            "BGRA",
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    // Static pad templates (portable across GStreamer-1.0 versions)
    static GstStaticPadTemplate sink_t = GST_STATIC_PAD_TEMPLATE(
        "sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw, format={BGRA}"));
    static GstStaticPadTemplate src_t = GST_STATIC_PAD_TEMPLATE(
        "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw, format={BGRA}"));
    gst_element_class_add_static_pad_template(ecls, &sink_t);
    gst_element_class_add_static_pad_template(ecls, &src_t);

    gst_element_class_set_static_metadata(ecls,
                                          "AMP Inference",
                                          "Filter/Effect/Video",
                                          "ONNX Runtime inference",
                                          "You <you@example.com>");

    bcls->start = gst_ampinfer_start;
    bcls->stop = gst_ampinfer_stop;
    bcls->set_caps = gst_ampinfer_set_caps;
    bcls->transform_ip = gst_ampinfer_transform_ip;
}

static void gst_ampinfer_init(GstAmpInfer *self) {
    self->opChainPath = nullptr;
    self->active = true;
    self->m = nullptr;
    gst_video_info_init(&self->vinfo);

    self->format = g_strdup("BGRA");
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(self), FALSE);
    gst_base_transform_set_qos_enabled(GST_BASE_TRANSFORM(self), FALSE);
}

static gboolean plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "ampinfer", GST_RANK_NONE, GST_TYPE_AMPINFER);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  ampinfer,
                  "AMP inference (YOLO + ONNX Runtime)",
                  plugin_init,
                  "1.0",
                  "LGPL",
                  "amp-elements",
                  "https://example.com")
