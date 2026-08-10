/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#include "gst/gstelement.h"
#include "gst/gstpad.h"
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <fmt/core.h>
#include <memory>
#include <variant>

#include "glib-object.h"
#include "glib.h"

#include "Log.h"
#include "pek/Perception.h"
#include "pek/Result.h"
#include "pek/Tools.h"

#include "op/OpChain.h"
#include "op/OpChainContext.h"

#include "gst/PerceptionMeta.h"
#include "mediaio/GstVideoFrame.h"
#include "perf/PerformanceTracer.h"

struct GstPekInferMembers {
    // std::shared_ptr<onnx::Inference> onnxInference;

    pek::op::OpChain opChain;

    pek::Result<void> executeOpChain(pek::op::OpChainContext &opChainContext) {
        return opChain.execute(opChainContext);
    }

    pek::Result<void> setupOpChainFromJson(const std::string &filePath) {
        auto setupResult = opChain.setupFromFile(filePath);
        if (!setupResult) {
            return setupResult;
        }
        return {};
    }
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
    gchar *format;
    gchar *inferId;

    gboolean qosEnabled;
    GstClockTime qosEarliestTime;
    guint64 processingSkipFrames;
    gdouble qosProportion;
    GstClockTime qosTimestamp;
    guint64 qosProcessed;
    guint64 qosDropped;
    guint64 qosGeneration;
    guint64 qosAcceptedEventsDebug;

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

static void gst_pekinfer_reset_qos_unlocked(GstPekInfer *self) {
    self->qosEarliestTime = GST_CLOCK_TIME_NONE;
    self->processingSkipFrames = 0;
    self->qosProportion = 1.0;
    self->qosTimestamp = GST_CLOCK_TIME_NONE;
    self->qosProcessed = 0;
    self->qosDropped = 0;
    ++self->qosGeneration;
}

static void gst_pekinfer_reset_qos(GstPekInfer *self) {
    GST_OBJECT_LOCK(self);
    gst_pekinfer_reset_qos_unlocked(self);
    GST_OBJECT_UNLOCK(self);
}

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

static bool gst_pekinfer_is_yuv_format(GstVideoFormat format) {
    return format == GST_VIDEO_FORMAT_I420 || format == GST_VIDEO_FORMAT_NV12 ||
           format == GST_VIDEO_FORMAT_YUY2;
}

static bool gst_pekinfer_has_supported_or_defaultable_yuv_colorimetry(const GstVideoInfo &info) {
    const GstVideoColorimetry colorimetry = GST_VIDEO_INFO_COLORIMETRY(&info);

    switch (colorimetry.matrix) {
    case GST_VIDEO_COLOR_MATRIX_UNKNOWN:
    case GST_VIDEO_COLOR_MATRIX_BT601:
    case GST_VIDEO_COLOR_MATRIX_BT709:
    case GST_VIDEO_COLOR_MATRIX_BT2020:
        break;
    default:
        return false;
    }

    switch (colorimetry.range) {
    case GST_VIDEO_COLOR_RANGE_UNKNOWN:
    case GST_VIDEO_COLOR_RANGE_0_255:
    case GST_VIDEO_COLOR_RANGE_16_235:
        return true;
    default:
        return false;
    }
}

static gboolean gst_pekinfer_start(GstBaseTransform *b) {
    auto *self = (GstPekInfer *)b;
    static pek::perf::PerformanceTracer *tracer = pek::perf::getGlobalTracer();
    (void)tracer;

    gst_pekinfer_reset_qos(self);

    self->m = new GstPekInferMembers();

    if (!self->opChainPath || !self->opChainPath[0]) {
        GST_ERROR_OBJECT(self, "opchain property is mandatory but not set");
        return FALSE;
    }

    auto setupResult = self->m->setupOpChainFromJson(self->opChainPath);
    if (!setupResult) {
        pek::log::error("Error while setting up op-chain [{}]: {}\n",
                        self->opChainPath,
                        setupResult.error().toString());

        GST_ELEMENT_ERROR(
            self, RESOURCE, FAILED, ("Failed to setup op-chain."), ("%s", self->opChainPath));

        return FALSE;
    }

    // Send model registration event downstream
    GstPad *srcpad = gst_element_get_static_pad(GST_ELEMENT(self), "src");
    if (srcpad) {
        GstStructure *structure = gst_structure_new("pek-model-register",
                                                    "model-name",
                                                    G_TYPE_STRING,
                                                    self->m->opChain.getName().c_str(),
                                                    "element-name",
                                                    G_TYPE_STRING,
                                                    GST_OBJECT_NAME(self),
                                                    "active",
                                                    G_TYPE_BOOLEAN,
                                                    gst_pekinfer_is_active(self),
                                                    NULL);
        if (!self->m->opChain.getDisplayName().empty()) {
            gst_structure_set(structure,
                              "display-name",
                              G_TYPE_STRING,
                              self->m->opChain.getDisplayName().c_str(),
                              NULL);
        }
        if (!self->m->opChain.getTask().empty()) {
            gst_structure_set(
                structure, "task", G_TYPE_STRING, self->m->opChain.getTask().c_str(), NULL);
        }
        if (!self->m->opChain.getRuntime().empty()) {
            gst_structure_set(
                structure, "runtime", G_TYPE_STRING, self->m->opChain.getRuntime().c_str(), NULL);
        }
        GstEvent *event = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, structure);
        gst_pad_push_event(srcpad, event);
        gst_object_unref(srcpad);
    }

    return TRUE;
}

static gboolean gst_pekinfer_stop(GstBaseTransform *b) {
    auto *self = (GstPekInfer *)b;
    gst_pekinfer_reset_qos(self);
    delete self->m;
    self->m = nullptr;
    return TRUE;
}

static gboolean gst_pekinfer_set_caps(GstBaseTransform *b, GstCaps *incaps, GstCaps *outcaps) {
    auto *self = (GstPekInfer *)b;
    (void)outcaps;

    if (!gst_video_info_from_caps(&self->vinfo, incaps)) {
        GST_ERROR_OBJECT(self, "Failed to parse input caps");
        return FALSE;
    }

    const auto format = GST_VIDEO_INFO_FORMAT(&self->vinfo);
    if (format != GST_VIDEO_FORMAT_BGRA && format != GST_VIDEO_FORMAT_RGB &&
        format != GST_VIDEO_FORMAT_I420 && format != GST_VIDEO_FORMAT_NV12 &&
        format != GST_VIDEO_FORMAT_YUY2) {
        GST_ERROR_OBJECT(self, "Unsupported format (expected BGRA, RGB, I420, NV12, or YUY2)");
        return FALSE;
    }

    if (gst_pekinfer_is_yuv_format(format) &&
        !gst_pekinfer_has_supported_or_defaultable_yuv_colorimetry(self->vinfo)) {
        const GstVideoColorimetry colorimetry = GST_VIDEO_INFO_COLORIMETRY(&self->vinfo);
        GST_ERROR_OBJECT(self,
                         "Unsupported YUV colorimetry/range (matrix=%d, range=%d)",
                         static_cast<int>(colorimetry.matrix),
                         static_cast<int>(colorimetry.range));
        return FALSE;
    }

    return TRUE;
}

static GstClockTime gst_pekinfer_saturating_add(GstClockTime timestamp, GstClockTime duration) {
    return duration >= GST_CLOCK_TIME_NONE - timestamp ? GST_CLOCK_TIME_NONE - 1
                                                       : timestamp + duration;
}

static gboolean gst_pekinfer_src_event(GstBaseTransform *trans, GstEvent *event) {
    auto *self = GST_PEKINFER(trans);
    const auto eventType = GST_EVENT_TYPE(event);

    if (eventType == GST_EVENT_QOS) {
        GST_OBJECT_LOCK(self);
        const gboolean handleQos = self->qosEnabled && self->active;
        const guint64 qosGeneration = self->qosGeneration;
        GST_OBJECT_UNLOCK(self);

        if (!handleQos)
            return GST_BASE_TRANSFORM_CLASS(gst_pekinfer_parent_class)->src_event(trans, event);

        GstQOSType type = GST_QOS_TYPE_UNDERFLOW;
        gdouble proportion = 1.0;
        GstClockTimeDiff diff = 0;
        GstClockTime timestamp = GST_CLOCK_TIME_NONE;
        gst_event_parse_qos(event, &type, &proportion, &diff, &timestamp);

        GstClockTime qosEarliestTime = GST_CLOCK_TIME_NONE;
        if (type == GST_QOS_TYPE_UNDERFLOW && diff > 0 && GST_CLOCK_TIME_IS_VALID(timestamp)) {
            const auto lateness = static_cast<GstClockTime>(diff);
            qosEarliestTime = gst_pekinfer_saturating_add(timestamp, lateness);
        }

        GST_OBJECT_LOCK(self);
        const gboolean publishQos =
            self->qosEnabled && self->active && qosGeneration == self->qosGeneration;
        if (publishQos) {
            self->qosEarliestTime = qosEarliestTime;
            self->qosProportion = proportion;
            self->qosTimestamp = timestamp;
            ++self->qosAcceptedEventsDebug;
        }
        GST_OBJECT_UNLOCK(self);

        if (!publishQos)
            return GST_BASE_TRANSFORM_CLASS(gst_pekinfer_parent_class)->src_event(trans, event);

        g_object_notify(G_OBJECT(self), "qos-accepted-events-debug");

        GST_DEBUG_OBJECT(self,
                         "Received QoS event: type=%d proportion=%f diff=%" G_GINT64_FORMAT
                         " timestamp=%" G_GUINT64_FORMAT,
                         type,
                         proportion,
                         diff,
                         timestamp);

        // This element owns the QoS policy for inference. Consuming the event
        // prevents upstream decoders from dropping the video buffer itself.
        gst_event_unref(event);
        return TRUE;
    }

    // Unrelated events keep the native path.
    return GST_BASE_TRANSFORM_CLASS(gst_pekinfer_parent_class)->src_event(trans, event);
}

static gboolean gst_pekinfer_sink_event(GstBaseTransform *trans, GstEvent *event) {
    const auto eventType = GST_EVENT_TYPE(event);
    switch (eventType) {
    case GST_EVENT_SEGMENT:
    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:
        gst_pekinfer_reset_qos(GST_PEKINFER(trans));
        break;
    default:
        break;
    }
    return GST_BASE_TRANSFORM_CLASS(gst_pekinfer_parent_class)->sink_event(trans, event);
}

struct GstPekInferFramePolicy {
    gboolean qosEnabled;
    gboolean skipInference;
    gdouble qosProportion;
    GstClockTimeDiff qosJitter;
    GstClockTime qosTimestamp;
    guint64 qosGeneration;
};

static GstPekInferFramePolicy gst_pekinfer_get_frame_policy(GstPekInfer *self,
                                                            GstClockTime runningTime) {
    GST_OBJECT_LOCK(self);
    const gboolean qosEnabled = self->qosEnabled;
    const gboolean qosSkip = GST_CLOCK_TIME_IS_VALID(runningTime) && qosEnabled &&
                             GST_CLOCK_TIME_IS_VALID(self->qosEarliestTime) &&
                             runningTime <= self->qosEarliestTime;
    const gboolean processingSkip = qosEnabled && self->processingSkipFrames > 0;
    if (processingSkip)
        --self->processingSkipFrames;
    if (GST_CLOCK_TIME_IS_VALID(runningTime) && GST_CLOCK_TIME_IS_VALID(self->qosEarliestTime) &&
        runningTime > self->qosEarliestTime) {
        self->qosEarliestTime = GST_CLOCK_TIME_NONE;
    }
    const GstPekInferFramePolicy policy{qosEnabled,
                                        qosSkip || processingSkip,
                                        qosSkip ? self->qosProportion : 1.0,
                                        qosSkip ? GST_CLOCK_DIFF(runningTime, self->qosEarliestTime)
                                                : 0,
                                        self->qosTimestamp,
                                        self->qosGeneration};
    GST_OBJECT_UNLOCK(self);
    return policy;
}

static GstFlowReturn gst_pekinfer_transform_ip(GstBaseTransform *b, GstBuffer *buf) {
    auto *self = (GstPekInfer *)b;

    if (!gst_pekinfer_is_active(self))
        return GST_FLOW_OK;

    // Keep the downstream metadata contract even when QoS skips inference.
    if (auto perceptionMeta = pek::PerceptionMeta::get(buf); !perceptionMeta) {
        auto perception = std::make_shared<pek::Perception>();
        if (!pek::PerceptionMeta::add(buf, perception)) {
            GST_WARNING_OBJECT(self, "Failed to attach PerceptionMeta");
            return GST_FLOW_OK;
        }
    }

    const GstClockTime runningTime =
        GST_BUFFER_PTS_IS_VALID(buf)
            ? gst_segment_to_running_time(&b->segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buf))
            : GST_CLOCK_TIME_NONE;

    const auto framePolicy = gst_pekinfer_get_frame_policy(self, runningTime);

    if (framePolicy.skipInference) {
        GST_OBJECT_LOCK(self);
        if (framePolicy.qosGeneration != self->qosGeneration) {
            GST_OBJECT_UNLOCK(self);
            return GST_FLOW_OK;
        }
        const guint64 processed = self->qosProcessed;
        const guint64 dropped = ++self->qosDropped;
        GST_OBJECT_UNLOCK(self);

        // Report the QoS action while returning OK so the video buffer still flows.
        GstMessage *message = gst_message_new_qos(
            GST_OBJECT(self),
            FALSE,
            runningTime,
            gst_segment_to_stream_time(&b->segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buf)),
            GST_BUFFER_PTS_IS_VALID(buf) ? GST_BUFFER_PTS(buf) : framePolicy.qosTimestamp,
            GST_BUFFER_DURATION(buf));
        gst_message_set_qos_values(
            message, framePolicy.qosJitter, framePolicy.qosProportion, GST_FORMAT_PERCENT_MAX);
        gst_message_set_qos_stats(message, GST_FORMAT_BUFFERS, processed, dropped);
        gst_element_post_message(GST_ELEMENT(self), message);
        return GST_FLOW_OK;
    }

    if (!self->m)
        return GST_FLOW_OK;

    const GstClockTime processingStartedAt =
        framePolicy.qosEnabled ? gst_util_get_timestamp() : GST_CLOCK_TIME_NONE;

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
    auto ret =
        pek::PerceptionMeta::mutate<GstFlowReturn>(buf, [self, sharedMediaFrame](auto &perception) {
            // Execute the op-chain with the provided context. The chain can read and mutate the
            // perception and read the video frame, but not mutate it.
            pek::op::OpChainContext opChainContext;
            opChainContext.inferenceInfo.inferElementId =
                std::string(gst_pekinfer_get_effective_inferId(self));

            opChainContext.perception = &perception;
            opChainContext.videoFrames["pipelineVideoFrame"] = sharedMediaFrame;

            auto executeResult = self->m->executeOpChain(opChainContext);
            if (!executeResult) {
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

    const GstClockTime processingLatency =
        framePolicy.qosEnabled ? gst_util_get_timestamp() - processingStartedAt : 0;
    GstClockTime frameDuration = GST_BUFFER_DURATION(buf);
    if (GST_VIDEO_INFO_FPS_N(&self->vinfo) > 0 && GST_VIDEO_INFO_FPS_D(&self->vinfo) > 0) {
        frameDuration = gst_util_uint64_scale(
            GST_SECOND, GST_VIDEO_INFO_FPS_D(&self->vinfo), GST_VIDEO_INFO_FPS_N(&self->vinfo));
    }
    GST_OBJECT_LOCK(self);
    const gboolean publishProcessing = framePolicy.qosEnabled && self->qosEnabled && self->active &&
                                       framePolicy.qosGeneration == self->qosGeneration;
    if (publishProcessing) {
        ++self->qosProcessed;
        if (GST_CLOCK_TIME_IS_VALID(frameDuration) && frameDuration > 0 &&
            processingLatency > frameDuration) {
            // Integer division already rounds down for nonmultiples. Subtracting one nanosecond
            // only changes exact multiples, treating a frame due exactly at completion as on time.
            self->processingSkipFrames = (processingLatency - 1) / frameDuration;
        }
    }
    GST_OBJECT_UNLOCK(self);

    return GST_FLOW_OK;
}

// ---------------- properties & class init ----------------

enum {
    PROP_0,
    PROP_OPCHAIN_PATH,
    PROP_MODEL_ACTIVE,
    PROP_FORMAT,
    PROP_INFER_ID,
    PROP_QOS_ENABLED,
    PROP_QOS_ACCEPTED_EVENTS_DEBUG,
};

static void gst_pekinfer_set_property(GObject *o, guint id, const GValue *v, GParamSpec *ps) {
    auto *self = (GstPekInfer *)o;
    switch (id) {
    case PROP_OPCHAIN_PATH:
        g_free(self->opChainPath);
        self->opChainPath = g_value_dup_string(v);
        break;
    case PROP_MODEL_ACTIVE: {
        GST_OBJECT_LOCK(self);
        self->active = g_value_get_boolean(v);
        if (!self->active)
            gst_pekinfer_reset_qos_unlocked(self);
        GST_OBJECT_UNLOCK(self);
        break;
    }
    case PROP_FORMAT:
        g_free(self->format);
        self->format = g_value_dup_string(v);
        break;
    case PROP_INFER_ID:
        g_free(self->inferId);
        self->inferId = g_value_dup_string(v);
        break;
    case PROP_QOS_ENABLED:
        GST_OBJECT_LOCK(self);
        self->qosEnabled = g_value_get_boolean(v);
        if (!self->qosEnabled)
            gst_pekinfer_reset_qos_unlocked(self);
        GST_OBJECT_UNLOCK(self);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void gst_pekinfer_get_property(GObject *o, guint id, GValue *v, GParamSpec *ps) {
    auto *self = (GstPekInfer *)o;
    switch (id) {
    case PROP_OPCHAIN_PATH:
        g_value_set_string(v, self->opChainPath);
        break;
    case PROP_MODEL_ACTIVE:
        g_value_set_boolean(v, gst_pekinfer_is_active(self));
        break;
    case PROP_FORMAT:
        g_value_set_string(v, self->format);
        break;
    case PROP_INFER_ID:
        g_value_set_string(v, gst_pekinfer_get_effective_inferId(self));
        break;
    case PROP_QOS_ENABLED:
        GST_OBJECT_LOCK(self);
        g_value_set_boolean(v, self->qosEnabled);
        GST_OBJECT_UNLOCK(self);
        break;
    case PROP_QOS_ACCEPTED_EVENTS_DEBUG:
        GST_OBJECT_LOCK(self);
        g_value_set_uint64(v, self->qosAcceptedEventsDebug);
        GST_OBJECT_UNLOCK(self);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void gst_pekinfer_finalize(GObject *object) {
    auto *self = reinterpret_cast<GstPekInfer *>(object);

    delete self->m;
    self->m = nullptr;
    g_clear_pointer(&self->opChainPath, g_free);
    g_clear_pointer(&self->format, g_free);
    g_clear_pointer(&self->inferId, g_free);

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
                            "Video format (BGRA, RGB, I420, NV12, or YUY2)",
                            "BGRA",
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_INFER_ID,
        g_param_spec_string("infer-id",
                            "ID of the inference element",
                            "ID of the inference element (used in Plumber to identify the layers. "
                            "Defaults to the name property of the element)",
                            "",
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_QOS_ENABLED,
        g_param_spec_boolean("qos-enabled",
                             "QoS enabled",
                             "Enable experimental inference skipping from QoS feedback",
                             false,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobj,
        PROP_QOS_ACCEPTED_EVENTS_DEBUG,
        g_param_spec_uint64("qos-accepted-events-debug",
                            "QoS accepted events debug",
                            "Debug-only count of QoS events committed by pekinfer",
                            0,
                            G_MAXUINT64,
                            0,
                            (GParamFlags)(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

    // Static pad templates (portable across GStreamer-1.0 versions)
    static GstStaticPadTemplate sink_t =
        GST_STATIC_PAD_TEMPLATE("sink",
                                GST_PAD_SINK,
                                GST_PAD_ALWAYS,
                                GST_STATIC_CAPS("video/x-raw, format={BGRA,RGB,I420,NV12,YUY2}"));
    static GstStaticPadTemplate src_t =
        GST_STATIC_PAD_TEMPLATE("src",
                                GST_PAD_SRC,
                                GST_PAD_ALWAYS,
                                GST_STATIC_CAPS("video/x-raw, format={BGRA,RGB,I420,NV12,YUY2}"));
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
    bcls->sink_event = gst_pekinfer_sink_event;
    bcls->src_event = gst_pekinfer_src_event;
    bcls->transform_ip = gst_pekinfer_transform_ip;
}

static void gst_pekinfer_init(GstPekInfer *self) {
    self->opChainPath = nullptr;
    self->active = true;
    self->m = nullptr;
    self->inferId = nullptr;
    self->qosEnabled = false;
    self->qosAcceptedEventsDebug = 0;
    self->qosEarliestTime = GST_CLOCK_TIME_NONE;
    self->processingSkipFrames = 0;
    self->qosProportion = 1.0;
    self->qosTimestamp = GST_CLOCK_TIME_NONE;
    self->qosProcessed = 0;
    self->qosDropped = 0;
    self->qosGeneration = 0;

    gst_video_info_init(&self->vinfo);

    self->format = g_strdup("BGRA");
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(self), FALSE);
    gst_base_transform_set_qos_enabled(GST_BASE_TRANSFORM(self), FALSE);
}

static gboolean pekinfer_plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "pekinfer", GST_RANK_NONE, GST_TYPE_PEKINFER);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  pekinfer,
                  "PEK inference (YOLO + ONNX Runtime)",
                  pekinfer_plugin_init,
                  "1.0",
                  "LGPL",
                  "pek-elements",
                  "https://example.com")
