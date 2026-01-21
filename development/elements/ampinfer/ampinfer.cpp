#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <fmt/core.h>
#include <memory>
#include <onnxruntime_cxx_api.h>

#include "amp/PerceptionContext.h"
#include "glib-object.h"
#include "glib.h"
#include "gst/gstpad.h"

#include "nlohmann/json.hpp"
#include "onnx/Inference.h"

#include "amp/AttributeMap.h"
#include "amp/DescriptorStrings.h"
#include "amp/File.h"
#include "amp/Labels.h"
#include "amp/Painter.h"
#include "amp/Result.h"
#include "amp/String.h"
#include "amp/Tools.h"

#include "op/Op.h"
#include "op/OpChain.h"
#include "op/OpChainContext.h"

#include <PerformanceTracer.h>

struct GstAmpInferMembers {
    std::shared_ptr<onnx::Inference> onnxInference;

    amp::OpChain opChain;

    amp::Result<void> executeOpChain(amp::OpChainContext &opChainContext,
                                     amp::PerceptionContext &perceptionContext) {
        return {};
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
    gchar *modelPath;
    gchar *opChainPath;
    gchar *modelName;
    gboolean active;

    // a safe place for c++ stuff
    GstAmpInferMembers *m;
};

G_END_DECLS

G_DEFINE_TYPE(GstAmpInfer, gst_ampinfer, GST_TYPE_BASE_TRANSFORM)

// ---------------- GstBaseTransform virtuals ----------------

static gboolean gst_ampinfer_start(GstBaseTransform *b) {
    auto *self = (GstAmpInfer *)b;
    static amp::PerformanceTracer *tracer = amp::getGlobalTracer();
    (void)tracer;

    if (!self->modelName) {
        GST_ERROR_OBJECT(self, "model-name property is mandatory but not set");
        return FALSE;
    }

    self->m = new GstAmpInferMembers();

    if (self->opChainPath && self->opChainPath[0]) {

        auto setupResult = self->m->setupOpChainFromJson(self->opChainPath);
        if (!setupResult) {
            fmt::print("Error while setting up op-chain: {}\n", setupResult.error().toString());
            AMP_ABORT;
        }

    } else {
        try {

            self->m->onnxInference = std::make_shared<onnx::Inference>();

            auto setupResult = self->m->onnxInference->setupFromJson(self->modelPath);
            if (!setupResult) {
                fmt::print("{}\n", setupResult.error().toString());
                AMP_ABORT;
            }
        } catch (const std::exception &e) {
            amp::Error err = AMP_ERROR(amp::ErrorFlag::OnnxStartupException, e.what());
            fmt::print("{}\n", err.toString());
            AMP_ABORT;
            return FALSE;
        }
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
    if (GST_VIDEO_INFO_FORMAT(&self->vinfo) != GST_VIDEO_FORMAT_RGB) {
        GST_ERROR_OBJECT(self, "Unsupported format (expected RGB)");
        return FALSE;
    }

    return TRUE;
}

static GstFlowReturn gst_ampinfer_transform_ip(GstBaseTransform *b, GstBuffer *buf) {
    auto *self = (GstAmpInfer *)b;

    if (!self->active)
        return GST_FLOW_OK;

    if (!self->m || !self->m->onnxInference)
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

    // Get tracer and prepare metric names
    static amp::PerformanceTracer *tracer = amp::getGlobalTracer();
    std::string base_name = self->modelName ? self->modelName : "ampinfer";
    std::string preprocess_name = base_name + "_preprocess";
    std::string inference_name = base_name + "_inference";
    std::string postprocess_name = base_name + "_postprocess";

    // preprocess
    {
        if (!self->active) {
            gst_buffer_unmap(buf, &map);
            return GST_FLOW_OK;
        }

        amp::PerformanceTracer::ScopedTimer timer(tracer, preprocess_name);
        auto prepocessResult = self->m->onnxInference->preprocessImageData(
            0, rgb, amp::DataKind::ImageRgbChw, amp::Tdt::Uint8, frameWidth, frameHeight);
        if (!prepocessResult) {
            fmt::print("{}\n", prepocessResult.error().toString());
            gst_buffer_unmap(buf, &map);
            return GST_FLOW_OK;
        }
    }

    // inference
    {
        if (!self->active) {
            gst_buffer_unmap(buf, &map);
            return GST_FLOW_OK;
        }

        amp::PerformanceTracer::ScopedTimer timer(tracer, inference_name);
        auto inferenceResult = self->m->onnxInference->inference();
        if (!inferenceResult) {
            fmt::print("{}\n", inferenceResult.error().toString());
            gst_buffer_unmap(buf, &map);
            AMP_ABORT;
            return GST_FLOW_OK;
        }
    }

    // postprocess
    amp::RawDetectionLayer detectionResults;
    {
        if (!self->active) {
            gst_buffer_unmap(buf, &map);
            return GST_FLOW_OK;
        }

        GST_LOG_OBJECT(self, "Recording postprocess metric: %s", postprocess_name.c_str());
        amp::PerformanceTracer::ScopedTimer timer(tracer, postprocess_name);

        amp::TensorParser::Settings settings;
        settings.iouThreshold = 0.3f;
        settings.confidenceThreshold = 0.5f;
        settings.normalizedCoordinates = false;
        settings.maxDetectionCount = 12;

        auto postprocessResult = self->m->onnxInference->postprocess(settings, detectionResults);
        if (!postprocessResult) {
            fmt::print("{}\n", postprocessResult.error().toString());
            gst_buffer_unmap(buf, &map);
            return GST_FLOW_OK;
        }
    }

    // decorate (in-place draw)
    if (false == detectionResults.maps.empty()) {
        amp::Painter painter(rgb, frameWidth, frameHeight, frameWidth * 3);
        painter.drawSegmentMap8(detectionResults.maps[0].map.data(),
                                detectionResults.maps[0].width,
                                detectionResults.maps[0].height);
    }

    if (detectionResults.rects.size()) {
        amp::Painter painter(rgb, frameWidth, frameHeight, frameWidth * 3);
        amp::TextRenderer textRenderer;

        if (self->m->onnxInference->getModel().modelFamily ==
            std::string(amp::NetworkId::YoloObjectDetection)) {
            for (const auto &a : detectionResults.rects) {
                painter.drawRect(a.x, a.y, a.w, a.h, 255, 123, 52, 2);
                auto label = amp::Labels::getLabel(amp::LabelType::Coco, a.classIndex);
                textRenderer.drawText(painter, a.x, a.y, label.data(), 0, 0, 0, 0, 255, 0);
            }
        }

        if (self->m->onnxInference->getModel().modelFamily ==
            std::string(amp::NetworkId::UltraFace)) {
            for (const auto &a : detectionResults.rects) {
                painter.drawCircle(a.x + a.w / 2, a.y + a.h / 2, a.w / 2, 155, 255, 64, 6);
            }
            for (const auto &a : detectionResults.points) {
                painter.drawPoint(a.x, a.y, 255, 255, 255, 4);
            }
        }
    }

    gst_buffer_unmap(buf, &map);
    return GST_FLOW_OK;
}

// ---------------- properties & class init ----------------

enum { PROP_0, PROP_MODEL_PATH, PROP_OPCHAIN_PATH, PROP_MODEL_NAME, PROP_MODEL_ACTIVE };

static void gst_ampinfer_set_property(GObject *o, guint id, const GValue *v, GParamSpec *ps) {
    auto *self = (GstAmpInfer *)o;
    switch (id) {
    case PROP_MODEL_PATH:
        g_free(self->modelPath);
        self->modelPath = g_value_dup_string(v);
        break;
    case PROP_OPCHAIN_PATH:
        g_free(self->opChainPath);
        self->opChainPath = g_value_dup_string(v);
        break;
    case PROP_MODEL_NAME:
        g_free(self->modelName);
        self->modelName = g_value_dup_string(v);
        break;
    case PROP_MODEL_ACTIVE: {
        gboolean new_active = g_value_get_boolean(v);
        if (self->active && !new_active && self->modelName) {
            static amp::PerformanceTracer *tracer = amp::getGlobalTracer();
            std::string base_name = self->modelName;
            GST_INFO_OBJECT(self, "Removing metrics for model: %s", base_name.c_str());
            tracer->removeMetrics(base_name + "_preprocess");
            tracer->removeMetrics(base_name + "_inference");
            tracer->removeMetrics(base_name + "_postprocess");
        }
        self->active = new_active;
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void gst_ampinfer_get_property(GObject *o, guint id, GValue *v, GParamSpec *ps) {
    auto *self = (GstAmpInfer *)o;
    switch (id) {
    case PROP_MODEL_PATH:
        g_value_set_string(v, self->modelPath);
        break;
    case PROP_OPCHAIN_PATH:
        g_value_set_string(v, self->opChainPath);
        break;
    case PROP_MODEL_NAME:
        g_value_set_string(v, self->modelName);
        break;
    case PROP_MODEL_ACTIVE:
        g_value_set_boolean(v, self->active);
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
        PROP_MODEL_PATH,
        g_param_spec_string("model-path",
                            "Model path",
                            "Path to YOLO ONNX model",
                            nullptr,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

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
        PROP_MODEL_NAME,
        g_param_spec_string("model-name",
                            "Model name",
                            "Name of the executed model (mandatory)",
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

    static GstStaticPadTemplate sink_t = GST_STATIC_PAD_TEMPLATE(
        "sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw, format=(string)RGB"));
    static GstStaticPadTemplate src_t = GST_STATIC_PAD_TEMPLATE(
        "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw, format=(string)RGB"));
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
    self->modelPath = nullptr;
    self->modelName = nullptr;
    self->active = true;
    self->m = nullptr;
    gst_video_info_init(&self->vinfo);

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
