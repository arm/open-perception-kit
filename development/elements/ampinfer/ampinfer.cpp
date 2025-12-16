
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>

#include <onnxruntime_cxx_api.h>

#include <fmt/core.h>

#include "glib-object.h"
#include "glib.h"
#include "gst/gstpad.h"
#include "uniflow/labels.h"
#include "uniflow/model_io.h"
#include "uniflow/public_types.h"
#include "uniflow/yolo_like_parser.h"
#include <fmt/core.h>

#include "onnx/Inference.h"

#include "amp/DescriptorStrings.h"
#include "amp/Painter.h"
#include "amp/Result.h"
#include "amp/Tools.h"
#include <PerformanceTracer.h>

struct GstAmpInferMembers {
    std::shared_ptr<onnx::Inference> onnxInference;
};

#ifndef PACKAGE
#define PACKAGE "amp-elements"
#endif

G_BEGIN_DECLS

#define GST_TYPE_AMPINFER (gst_ampinfer_get_type())
G_DECLARE_FINAL_TYPE(GstAmpInfer, gst_ampinfer, GST, AMPINFER, GstVideoFilter)

struct _GstAmpInfer {
    GstVideoFilter parent;
    GstVideoInfo in_info;

    // Properties
    gchar *modelPath;
    gchar *modelName;
    gboolean active;

    // a safe place for c++ stuff
    GstAmpInferMembers *m;
};

G_END_DECLS

G_DEFINE_TYPE(GstAmpInfer, gst_ampinfer, GST_TYPE_VIDEO_FILTER)

// ---------------- Gst virtuals ----------------

static gboolean gst_ampinfer_start(GstBaseTransform *b) {
    auto *self = (GstAmpInfer *)b;
    static amp::PerformanceTracer *tracer = amp::getGlobalTracer();

    if (!self->modelName) {
        GST_ERROR_OBJECT(self, "model-name property is mandatory but not set");
        return FALSE;
    }

    self->m = new GstAmpInferMembers();

    try {
        self->m->onnxInference = std::make_shared<onnx::Inference>();

        auto setupResult = self->m->onnxInference->setupFromJson(self->modelPath);
        if (!setupResult) {
            fmt::print("{}\n", setupResult.error().toString());
            amp::Tools::abort();
        }
    } catch (const std::exception &e) {
        amp::Error err = AMP_ERROR(amp::ErrorFlag::OnnxLowLevelError, e.what());
        fmt::print("{}\n", err.toString());
        amp::Tools::abort();
        return FALSE;
    }

    // Send model registration event downstream
    GstPad *srcpad = gst_element_get_static_pad(GST_ELEMENT(self), "src");
    if (srcpad) {
        GstStructure *structure = gst_structure_new("amp-model-register",
                                                    "model-name",
                                                    G_TYPE_STRING,
                                                    self->modelName,
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

    // Send model unregistration event downstream
    GstPad *srcpad = gst_element_get_static_pad(GST_ELEMENT(self), "src");
    if (srcpad) {
        GstStructure *structure = gst_structure_new(
            "amp-model-unregister", "element-name", G_TYPE_STRING, GST_OBJECT_NAME(self), NULL);
        GstEvent *event = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, structure);
        gst_pad_push_event(srcpad, event);
        gst_object_unref(srcpad);
    }

    delete self->m;
    return TRUE;
}

static gboolean gst_ampinfer_set_info(GstVideoFilter *vf,
                                      GstCaps *incaps,
                                      GstVideoInfo *ininfo,
                                      GstCaps *outcaps,
                                      GstVideoInfo *outinfo) {
    (void)incaps;
    (void)outcaps;
    (void)outinfo;

    auto *self = (GstAmpInfer *)vf;
    self->in_info = *ininfo;
    return TRUE;
}

static GstFlowReturn gst_ampinfer_transform_frame_ip(GstVideoFilter *vf, GstVideoFrame *frame) {
    GstAmpInfer *self = (GstAmpInfer *)vf;

    if (false == self->active)
        return GST_FLOW_OK;

    size_t frameWidth = frame->info.width;
    size_t frameHeight = frame->info.height;
    uint8_t *rgb = (uint8_t *)frame->data[0];
    if (!rgb)
        return GST_FLOW_OK;

    if (!self->m->onnxInference)
        return GST_FLOW_OK;

    // Get tracer and prepare metric names
    static amp::PerformanceTracer *tracer = amp::getGlobalTracer();
    std::string base_name = self->modelName ? self->modelName : "ampinfer";
    std::string preprocess_name = base_name + "_preprocess";
    std::string inference_name = base_name + "_inference";
    std::string postprocess_name = base_name + "_postprocess";

    // preprocess
    {
        amp::PerformanceTracer::ScopedTimer timer(tracer, preprocess_name);
        auto prepocessResult =
            self->m->onnxInference->preprocessImageData(0,
                                                        rgb,
                                                        uflw::TensorDataKind::ImageRgbChw,
                                                        uflw::ValueType::u8,
                                                        frameWidth,
                                                        frameHeight);
        if (!prepocessResult) {
            fmt::print("{}", prepocessResult.error().toString());
            return GST_FLOW_OK;
        }
    }

    // inference
    {
        amp::PerformanceTracer::ScopedTimer timer(tracer, inference_name);
        auto inferenceResult = self->m->onnxInference->inference();
        if (!inferenceResult) {
            fmt::print("{}", inferenceResult.error().toString());
            return GST_FLOW_OK;
        }
    }

    // postprocess
    uflw::DetectionResult detectionResults;
    {
        amp::PerformanceTracer::ScopedTimer timer(tracer, postprocess_name);
        uflw::NetworkOutputParser::Settings settings;
        settings.iouThreshold = 0.3f;
        settings.confidenceThreshold = 0.5f;
        settings.normalizedCoordinates = false;
        settings.maxDetectionCount = 12;

        auto postprocessResult = self->m->onnxInference->postprocess(settings, detectionResults);
        if (!postprocessResult) {
            fmt::print("{}", postprocessResult.error().toString());
            return GST_FLOW_OK;
        }
    }

    // decorate
    if (detectionResults.rects.size()) {

        amp::Painter painter(rgb, frameWidth, frameHeight, frameWidth * 3);
        amp::TextRenderer textRenderer;

        if (self->m->onnxInference->getModel().modelFamily ==
            std::string(amp::NetworkId::YoloObjectDetection)) {
            for (const auto &a : detectionResults.rects) {
                painter.drawRect(a.x, a.y, a.w, a.h, 255, 123, 52, 2);
                auto label = uflw::Labels::getLabel(uflw::LabelType::Coco, a.classIndex);
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

    return GST_FLOW_OK;
}

// ---------------- properties & class init ----------------

enum { PROP_0, PROP_MODEL_PATH, PROP_MODEL_NAME, PROP_MODEL_ACTIVE };

static void gst_ampinfer_set_property(GObject *o, guint id, const GValue *v, GParamSpec *ps) {
    auto *self = (GstAmpInfer *)o;
    switch (id) {
    case PROP_MODEL_PATH:
        g_free(self->modelPath);
        self->modelPath = g_value_dup_string(v);
        break;
    case PROP_MODEL_NAME:
        g_free(self->modelName);
        self->modelName = g_value_dup_string(v);
        break;
    case PROP_MODEL_ACTIVE:
        self->active = g_value_get_boolean(v);
        break;
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
    GstVideoFilterClass *vcls = GST_VIDEO_FILTER_CLASS(klass);
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
                             "Do or not to do",
                             true,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    // Static pad templates (portable across GStreamer-1.0 versions)
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

    // gst_base_transform_class_set_in_place (bcls, TRUE);
    vcls->set_info = gst_ampinfer_set_info;
    vcls->transform_frame_ip = gst_ampinfer_transform_frame_ip;
}

static void gst_ampinfer_init(GstAmpInfer *self) {
    self->modelPath = nullptr;
    self->modelName = nullptr;
    self->active = true;
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
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
