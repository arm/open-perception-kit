
#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>

#include <onnxruntime_cxx_api.h>

#include "amp/Tools.h"
#include "uniflow/blazeface_parser.h"
#include "uniflow/image_tensor_builder.h"
#include "uniflow/model_io.h"
#include "uniflow/output_types.h"
#include "uniflow/public_types.h"
#include "uniflow/tensor_view.h"
#include "uniflow/yolo_like_parser.h"
#include "uniflow/labels.h"

#include "onnx/Inference.h"

#include "amp/Painter.h"

#include "gst/Tools.h"
#include "PerformanceMetrics.h"

#include <fmt/core.h>

#ifndef PACKAGE
#define PACKAGE "amp-elements"
#endif

G_BEGIN_DECLS

#define GST_TYPE_AMPINFER (gst_ampinfer_get_type())
G_DECLARE_FINAL_TYPE (GstAmpInfer, gst_ampinfer, GST, AMPINFER, GstVideoFilter)

struct _GstAmpInfer {
  GstVideoFilter parent;
  GstVideoInfo   in_info;

  // Properties
  gchar* modelPath;

  std::shared_ptr<onnx::Inference> onnxInference;
};

G_END_DECLS

G_DEFINE_TYPE (GstAmpInfer, gst_ampinfer, GST_TYPE_VIDEO_FILTER)

// ---------------- Gst virtuals ----------------

static gboolean gst_ampinfer_start (GstBaseTransform *b) {
  auto *self = (GstAmpInfer*) b;

  try {
    self->onnxInference = std::make_shared<onnx::Inference>();
//    self->onnxInference->setup(self->modelPath);
    
    auto setupResult = self->onnxInference->setupFromJson(self->modelPath);
    if(!setupResult) {
      //fmt::print("{}\n", setupResult.error().toString());
      amp::Tools::abort();
    }

    if(self->onnxInference->getModel().modelFamily == uflw::ModelFamily::YoloObjectDetection) {
      std::unique_ptr<uflw::NetworkOutputParser> parser = std::make_unique<uflw::YoloLikeParser>();
      self->onnxInference->setOutputParser(std::move(parser));
    } else {
      std::unique_ptr<uflw::NetworkOutputParser> parser = std::make_unique<uflw::BlazeFaceParser>();
      self->onnxInference->setOutputParser(std::move(parser));
    }

    std::unique_ptr<uflw::NetworkInputBuilder> builder = std::make_unique<uflw::ImageTensorBuilder>();
    self->onnxInference->setInputBuilder(std::move(builder));

    // ---

    // cache I/O names (works with ONNX Runtime 1.18+)
    Ort::AllocatorWithDefaultOptions alloc;
    
    GST_INFO_OBJECT(self, "Loaded model: %s", self->modelPath);
  } catch (const std::exception& e) {
    GST_ERROR_OBJECT(self, "ONNX init failed: %s", e.what());
    return FALSE;
  }

  return TRUE;
}

static gboolean gst_ampinfer_stop (GstBaseTransform *b) {
  auto *self = (GstAmpInfer*) b;
  return TRUE;
}

static gboolean gst_ampinfer_set_info (GstVideoFilter *vf,
                       GstCaps *incaps, GstVideoInfo *ininfo,
                       GstCaps *outcaps, GstVideoInfo *outinfo)
{
  (void) incaps;
  (void) outcaps;
  (void) outinfo;

  auto *self = (GstAmpInfer*) vf;
  self->in_info = *ininfo;
  return TRUE;
}

static GstFlowReturn gst_ampinfer_transform_frame_ip (GstVideoFilter *vf, GstVideoFrame *frame)
{
  auto *self = (GstAmpInfer*) vf;
  
  size_t frameWidth = frame->info.width;
  size_t frameHeight = frame->info.height;

  uint8_t* rgb = (uint8_t*)frame->data[0];
  if (!rgb) return GST_FLOW_OK;

  if(self->onnxInference) { 

    self->onnxInference->preprocessImageData(
    0, 
    rgb, uflw::TensorDataKind::ImageRgbChw, uflw::ValueType::u8, 
    frameWidth, frameHeight);

    self->onnxInference->inference();

    uflw::NetworkOutputParser::Settings settings;
    settings.confidenceThreshold = 0.3f;
    settings.normalizedCoordinates = false;
    settings.iouThreshold = 0.3f;
    settings.maxDetectionCount = 3;
    uflw::DetectionResult detectionResults;
    self->onnxInference->postprocess(settings, detectionResults);

    if(detectionResults.rects.size()) {

      amp::Painter painter(rgb, frameWidth, frameHeight, frameWidth * 3);
      amp::TextRenderer textRenderer;

      if(self->onnxInference->getModel().modelFamily == uflw::ModelFamily::YoloObjectDetection) {
        for(const auto& a : detectionResults.rects) {            
          painter.drawRect(a.x, a.y, a.w, a.h, 255, 123, 52, 2);

//          textRenderer.drawText(painter, 100, 200, "alma: korte", 255, 255, 255, 0, 255, 0);

            auto label = uflw::Labels::getLabel(uflw::LabelType::Coco, a.classIndex);
            textRenderer.drawText(painter, a.x, a.y, label.data(), 0, 0, 0, 0, 255, 0);

        }
      } else {
        for(const auto& a : detectionResults.rects) {
          painter.drawPoint(a.x + a.w / 2, a.y + a.h / 2, 100, 200, 255, 10);
          break;
        }
      }

      for(const auto& a : detectionResults.points) {
        painter.drawPoint(a.x, a.y, 255, 255, 255, 4);
      }
    }

  }

  return GST_FLOW_OK;
}

// ---------------- properties & class init ----------------

enum { PROP_0, PROP_MODEL_PATH, PROP_IMGSZ, PROP_CONF, PROP_IOU };

static void gst_ampinfer_set_property(GObject *o, guint id, const GValue *v, GParamSpec *ps) {
  auto *self = (GstAmpInfer*) o;
  switch (id) {
    case PROP_MODEL_PATH:
      g_free(self->modelPath);
      self->modelPath = g_value_dup_string(v);
      break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
  }
}

static void gst_ampinfer_get_property(GObject *o, guint id, GValue *v, GParamSpec *ps) {
  auto *self = (GstAmpInfer*) o;
  switch (id) {
    case PROP_MODEL_PATH: g_value_set_string(v, self->modelPath); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
  }
}

static void gst_ampinfer_class_init (GstAmpInferClass *klass) {
  GObjectClass *gobj = G_OBJECT_CLASS(klass);
  GstElementClass *ecls = GST_ELEMENT_CLASS(klass);
  GstVideoFilterClass *vcls = GST_VIDEO_FILTER_CLASS(klass);
  GstBaseTransformClass *bcls = GST_BASE_TRANSFORM_CLASS(klass);

  gobj->set_property = gst_ampinfer_set_property;
  gobj->get_property = gst_ampinfer_get_property;

  g_object_class_install_property(gobj, PROP_MODEL_PATH,
    g_param_spec_string("model-path","Model path","Path to YOLO ONNX model",
      nullptr, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  // Static pad templates (portable across GStreamer-1.0 versions)
  static GstStaticPadTemplate sink_t = GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, format=(string)RGB"));
  static GstStaticPadTemplate src_t  = GST_STATIC_PAD_TEMPLATE ("src",  GST_PAD_SRC,  GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, format=(string)RGB"));
  gst_element_class_add_static_pad_template (ecls, &sink_t);
  gst_element_class_add_static_pad_template (ecls, &src_t);

  gst_element_class_set_static_metadata (ecls,
    "AMP Inference", "Filter/Effect/Video",
    "ONNX Runtime inference", "You <you@example.com>");

  bcls->start = gst_ampinfer_start;
  bcls->stop  = gst_ampinfer_stop;

  //gst_base_transform_class_set_in_place (bcls, TRUE);
  vcls->set_info = gst_ampinfer_set_info;
  vcls->transform_frame_ip = gst_ampinfer_transform_frame_ip;
}

static void gst_ampinfer_init (GstAmpInfer *self) {
  self->modelPath = nullptr;
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (self), TRUE);
  gst_base_transform_set_qos_enabled(GST_BASE_TRANSFORM(self), FALSE);
}

static gboolean plugin_init (GstPlugin *plugin) {
  return gst_element_register(plugin, "ampinfer", GST_RANK_NONE, GST_TYPE_AMPINFER);
}

GST_PLUGIN_DEFINE(
  GST_VERSION_MAJOR, GST_VERSION_MINOR,
  ampinfer, "AMP inference (YOLO + ONNX Runtime)",
  plugin_init, "1.0", "LGPL", "amp-elements", "https://example.com"
)

