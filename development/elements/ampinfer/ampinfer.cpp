
#include <cstddef>
#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>

#include <memory>
#include <onnxruntime_cxx_api.h>

#include <stdio.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "OnnxInference.h"
#include "OnnxTools.h"
#include "GstTools.h"
#include "PerformanceMetrics.h"
#include "uniflow/blazeface_parser.h"
#include "uniflow/image_tensor_builder.h"
#include "uniflow/model_io.h"
#include "uniflow/public_types.h"
#include "uniflow/tensor_view.h"
#include "uniflow/yolo_like_parser.h"

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
  gchar  *model_path;
  gint    imgsz;
  gfloat  conf_thr;
  gfloat  iou_thr;

  // ORT
  gboolean           ort_ready;
  Ort::Env*          env; //{ORT_LOGGING_LEVEL_WARNING, "ampinfer"};
  Ort::Session*      session = nullptr;
  Ort::SessionOptions* session_opts;
  Ort::MemoryInfo*   mem_info; //{Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU)};
  std::vector<char*> input_names;
  std::vector<char*> output_names;

  std::shared_ptr<OnnxInference> onnxInference;
};

G_END_DECLS

G_DEFINE_TYPE (GstAmpInfer, gst_ampinfer, GST_TYPE_VIDEO_FILTER)

// ---------------- Gst virtuals ----------------

static gboolean gst_ampinfer_start (GstBaseTransform *b) {
  auto *self = (GstAmpInfer*) b;

  try {
    self->session_opts = new Ort::SessionOptions();
    self->session_opts->SetIntraOpNumThreads(1);
    self->env = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "ampinfer");
    self->mem_info = new Ort::MemoryInfo(Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU));
    self->session = new Ort::Session(*self->env, self->model_path, *self->session_opts);

    // ---

    self->onnxInference = std::make_shared<OnnxInference>();
    self->onnxInference->setup(self->model_path);

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
    const size_t ni = self->session->GetInputCount();
    const size_t no = self->session->GetOutputCount();

    self->input_names.clear();
    self->output_names.clear();
    for (size_t i=0;i<ni;++i) {
      auto s = self->session->GetInputNameAllocated(i, alloc);
      printf("**** Input:[%s]\n", s.get());
      self->input_names.push_back(strdup(s.get()));
    }
    for(size_t i = 0; i < no; ++i) {
      auto s = self->session->GetOutputNameAllocated(i, alloc);
      printf("**** Output:[%s]\n", s.get());
      self->output_names.push_back(strdup(s.get()));
    }
    
    self->ort_ready = TRUE;
    GST_INFO_OBJECT(self, "Loaded model: %s", self->model_path);
  } catch (const std::exception& e) {
    GST_ERROR_OBJECT(self, "ONNX init failed: %s", e.what());
    return FALSE;
  }

  return TRUE;
}

static gboolean gst_ampinfer_stop (GstBaseTransform *b) {
  auto *self = (GstAmpInfer*) b;
  if (self->session) { delete self->session; self->session = nullptr; }
  for (auto *p : self->input_names)  free(p);
  for (auto *p : self->output_names) free(p);
  self->input_names.clear();
  self->output_names.clear();
  self->ort_ready = FALSE;
  return TRUE;
}

static inline void drawBox(guint8* rgb, int framew, int frameh,
                           float x, float y, float w, float h)
{
    if (!rgb || framew <= 0 || frameh <= 0 || w <= 0 || h <= 0)
        return;

    // Convert to integer pixel coordinates
    int x0 = (int)x;
    int y0 = (int)y;
    int x1 = (int)(x + w);
    int y1 = (int)(y + h);

    // Clamp to frame
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > framew)  x1 = framew;
    if (y1 > frameh)  y1 = frameh;

    if (x0 >= x1 || y0 >= y1)
        return;

    // Helper lambda to set one pixel to white
    auto put_pixel = [&](int xx, int yy) {
        if (xx < 0 || xx >= framew || yy < 0 || yy >= frameh) return;
        int idx = 3 * (yy * framew + xx);
        rgb[idx + 0] = 0xff;
        rgb[idx + 1] = 0xff;
        rgb[idx + 2] = 0xff;
    };

    // Top edge (y0)
    for (int xx = x0; xx < x1; ++xx)
        put_pixel(xx, y0);

    // Bottom edge (y1 - 1)
    for (int xx = x0; xx < x1; ++xx)
        put_pixel(xx, y1 - 1);

    // Left edge (x0)
    for (int yy = y0; yy < y1; ++yy)
        put_pixel(x0, yy);

    // Right edge (x1 - 1)
    for (int yy = y0; yy < y1; ++yy)
        put_pixel(x1 - 1, yy);
}

static inline void drawPoint(guint8* rgb, int framew, int frameh, int x, int y)
{
    if (!rgb || x < 0 || y < 0 || x >= framew || y >= frameh)
        return;

    //printf("%d %d\n", x, y);

    size_t index = (framew * y + x) * 3;
    rgb[index + 0] = 0xff;
    rgb[index + 1] = 0x44;
    rgb[index + 2] = 0x22;
}

static inline void drawFace(guint8* rgb, int framew, int frameh,
                           float x, float y, float w, float h)
{
    if (!rgb || framew <= 0 || frameh <= 0 || w <= 0 || h <= 0)
        return;

    // Convert to integer pixel coordinates
    int x0 = (int)x;
    int y0 = (int)y;
    int x1 = (int)(x + w);
    int y1 = (int)(y + h);

    // Clamp to frame
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > framew)  x1 = framew;
    if (y1 > frameh)  y1 = frameh;

    if (x0 >= x1 || y0 >= y1)
        return;

    // Helper lambda to set one pixel to white
    auto put_pixel = [&](int xx, int yy) {
        if (xx < 0 || xx >= framew || yy < 0 || yy >= frameh) return;
        int idx = 3 * (yy * framew + xx);
        rgb[idx + 0] = 0xff;
        rgb[idx + 1] = 0x00;
        rgb[idx + 2] = 0xff;
    };

    //for (int yy = y0; yy < y1; ++yy)
      //for(int xx = x0; xx < x1; ++xx)
        //put_pixel(xx, yy);

    // Top edge (y0)
    for (int xx = x0; xx < x1; ++xx)
        put_pixel(xx, y0);

    // Bottom edge (y1 - 1)
    for (int xx = x0; xx < x1; ++xx)
        put_pixel(xx, y1 - 1);

    // Left edge (x0)
    for (int yy = y0; yy < y1; ++yy)
        put_pixel(x0, yy);

    // Right edge (x1 - 1)
    for (int yy = y0; yy < y1; ++yy)
       put_pixel(x1 - 1, yy); 
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

// --------------------------------------------------------------

// --------------------------------------------------------------

static GstFlowReturn gst_ampinfer_transform_frame_ip (GstVideoFilter *vf, GstVideoFrame *frame)
{
  auto *self = (GstAmpInfer*) vf;
  if (!self->ort_ready) return GST_FLOW_OK;

  size_t yoloSquareSize =(size_t)self->imgsz;
  
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

      if(self->onnxInference->getModel().modelFamily == uflw::ModelFamily::YoloObjectDetection) {
        for(const auto& a : detectionResults.rects) {
          drawBox(rgb, frameWidth, frameHeight, a.x, a.y, a.w, a.h);
          drawBox(rgb, frameWidth, frameHeight, a.x + 1, a.y + 1, a.w - 2, a.h - 2);
        }
      } else {
        for(const auto& a : detectionResults.rects) {
          drawFace(rgb, frameWidth, frameHeight, a.x, a.y, a.w, a.h);
          drawFace(rgb, frameWidth, frameHeight, a.x + 1, a.y + 1, a.w - 2, a.h - 2);
          break;
        }
      }

      for(const auto& a : detectionResults.points) {

        for(int y = -3; y <= 3; y++) {
          for(int x = -3; x <= 3; x++) {
            //drawPoint(rgb, frameWidth, frameHeight, (int)a.x + x, (int)a.y + y);
          }
        }
      }

        /*if(self->onnxInference->getModel().modelFamily == uflw::ModelFamily::YoloObjectDetection)
          drawBox(rgb, frameWidth, frameHeight, a.x, a.y, a.w, a.h);
        else
          drawFace(rgb, frameWidth, frameHeight, a.x, a.y, a.w, a.h);*/
    }

  }

  // ---- 

#ifdef ff
    self->onnxInference->EnsureInputOutputShape(0, yoloSquareSize * yoloSquareSize);

    //std::vector<float> inputTensorBuffer;
    //inputTensorBuffer.resize(yoloSquareSize * yoloSquareSize * 3);

    uflw::NetworkInputBuilder::Setup setup;
    setup.originals[0].data = rgb;
    setup.originals[0].width = frameWidth;
    setup.originals[0].height = frameHeight;
    setup.originals[0].byteCount = frameWidth * frameHeight * 3;
    setup.originals[0].kind = uflw::TensorDataKind::ImageRgbChw;
    setup.originals[0].type = uflw::ValueType::u8;

    //setup.targets[0].data = (uint8_t*)inputTensorBuffer.data();
    setup.targets[0].data = self->onnxInference->inputTensors[0]->getRawData();
    setup.targets[0].byteCount = self->onnxInference->inputTensors[0]->getByteCount();
    setup.targets[0].width = yoloSquareSize;
    setup.targets[0].height = yoloSquareSize;
    setup.targets[0].kind = uflw::TensorDataKind::ImageRgbChw;
    setup.targets[0].type = uflw::ValueType::f32;

    uflw::Result result = self->onnxInference->inputBuilder->build(setup);
    if(uflw::Result::Ok != result) {
      printf("ERROR!\n");
    }

  

  // ----

  std::array<int64_t,4> ishape{{1, 3, (int64_t)yoloSquareSize, (int64_t)yoloSquareSize }};
  Ort::Value in = Ort::Value::CreateTensor<float>(*self->mem_info,
    inputTensorBuffer.data(), inputTensorBuffer.size(),
    ishape.data(), ishape.size());

  // run
  uint64_t beforeInference = getNanos();
  std::vector<Ort::Value> out = self->session->Run(
    Ort::RunOptions{nullptr},
    (const char* const*)self->input_names.data(), &in, 1,
    (const char* const*)self->output_names.data(), self->output_names.size());
  uint64_t afterInference = getNanos();

  static uint64_t frameTime = 0;
  uint64_t now = getNanos();
  double frameDelay = double(now - frameTime);
  frameTime = now;
  
  static int inferenceP50 = 0;
  static int inferenceP95 = 0;
  static std::vector<int> inferenceSpans;
  inferenceSpans.push_back((int)((afterInference - beforeInference) / 1e6));

  if(inferenceSpans.size() >= 25) {
    std::sort(inferenceSpans.begin(), inferenceSpans.end());
    inferenceP50 = inferenceSpans[12];
    inferenceP95 = inferenceSpans[24];
    inferenceSpans.clear();
  }

  char buffer[128];
  sprintf(buffer, "Frame: %dx%d Tensor: %dx%d\nPlayback FPS: %.2f\nInference %dms Inference FPS: %.2f\nInference p50: %dms p95: %dms",
    (int)frameWidth, (int)frameHeight, (int)yoloSquareSize, (int)yoloSquareSize,
    (float)(1e9 / (double)(frameDelay)),
    (int)((double)(afterInference - beforeInference) / 1000000.0f),
    (float)(1e9 / (double)(afterInference - beforeInference)),
    inferenceP50, inferenceP95
  );

  static bool overlayDesignSetupDone = false;
  GstElement* overlay = GstTools::getOverlayElement(vf);
  if(overlay) {
    if(!overlayDesignSetupDone) {
      overlayDesignSetupDone = true;
      g_object_set(overlay,
        "font-desc", "Monospace, 7",
        "halignment", 0,
        "valignment", 2,
        "shaded-background", TRUE,
        "shading-value", 100,
        NULL);
    }

    g_object_set(overlay, "text", buffer, NULL);

    GstTools::releaseElement(overlay);
  }

  // ----------------------------------------------------------------
  
  {

     Ort::Value& v = out.at(0);

    auto info  = v.GetTensorTypeAndShapeInfo();
    std::vector<int64_t> dims = info.GetShape();

    float* p = v.GetTensorMutableData<float>();
    ONNXTensorElementDataType tensorType = info.GetElementType();

    auto tensorInfo = v.GetTensorTypeAndShapeInfo();
    size_t elemCount = tensorInfo.GetElementCount();
    size_t byteCount = sizeof(float) * elemCount;

    uflw::Shape shape(dims[0], dims[1], dims[2]);
    uflw::TensorReader tensor(p, byteCount, shape, uflw::ValueType::f32, 1.0f, 0.0f);

    uflw::NetworkOutputParser::Setup parseSetup;
    parseSetup.videoSettings.frameWidth = GST_VIDEO_FRAME_WIDTH(frame);
    parseSetup.videoSettings.frameHeight = GST_VIDEO_FRAME_HEIGHT(frame);
    parseSetup.videoSettings.modelInputWidth = self->imgsz;
    parseSetup.videoSettings.modelInputHeight = self->imgsz;
    parseSetup.tensor0 = &tensor;  

    uflw::DetectionResult detections;
    self->onnxInference->outputParser->parse(&detections, parseSetup);

    //printf("%d\n", (int)detections.rects.size());
    for(const auto& a : detections.rects) {
      drawBox(rgb, frameWidth, frameHeight, a.x, a.y, a.w, a.h);
    }

  }
#endif

  // ----------------------------------------------------------------


  return GST_FLOW_OK;
}

// ---------------- properties & class init ----------------

enum { PROP_0, PROP_MODEL_PATH, PROP_IMGSZ, PROP_CONF, PROP_IOU };

static void gst_ampinfer_set_property(GObject *o, guint id, const GValue *v, GParamSpec *ps) {
  auto *self = (GstAmpInfer*) o;
  switch (id) {
    case PROP_MODEL_PATH:
      g_free(self->model_path);
      self->model_path = g_value_dup_string(v);
      break;
    case PROP_IMGSZ: self->imgsz = g_value_get_int(v); break;
    case PROP_CONF:  self->conf_thr = g_value_get_float(v); break;
    case PROP_IOU:   self->iou_thr  = g_value_get_float(v); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
  }
}

static void gst_ampinfer_get_property(GObject *o, guint id, GValue *v, GParamSpec *ps) {
  auto *self = (GstAmpInfer*) o;
  switch (id) {
    case PROP_MODEL_PATH: g_value_set_string(v, self->model_path); break;
    case PROP_IMGSZ: g_value_set_int(v, self->imgsz); break;
    case PROP_CONF:  g_value_set_float(v, self->conf_thr); break;
    case PROP_IOU:   g_value_set_float(v, self->iou_thr); break;
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
  g_object_class_install_property(gobj, PROP_IMGSZ,
    g_param_spec_int("imgsz","Image size","Square input size (pixels)",
      160, 1024, 320, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobj, PROP_CONF,
    g_param_spec_float("conf","Confidence","Score threshold",
      0.0, 1.0, 0.25, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobj, PROP_IOU,
    g_param_spec_float("iou","IoU","NMS IoU threshold",
      0.0, 1.0, 0.45, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  // Static pad templates (portable across GStreamer-1.0 versions)
  static GstStaticPadTemplate sink_t = GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, format=(string)RGB"));
  static GstStaticPadTemplate src_t  = GST_STATIC_PAD_TEMPLATE ("src",  GST_PAD_SRC,  GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, format=(string)RGB"));
  gst_element_class_add_static_pad_template (ecls, &sink_t);
  gst_element_class_add_static_pad_template (ecls, &src_t);

  gst_element_class_set_static_metadata (ecls,
    "AMP YOLO Infer", "Filter/Effect/Video",
    "Tiny ONNX Runtime YOLO inference", "You <you@example.com>");

  bcls->start = gst_ampinfer_start;
  bcls->stop  = gst_ampinfer_stop;

  //gst_base_transform_class_set_in_place (bcls, TRUE);
  vcls->set_info = gst_ampinfer_set_info;
  vcls->transform_frame_ip = gst_ampinfer_transform_frame_ip;
}

static void gst_ampinfer_init (GstAmpInfer *self) {
  self->model_path = nullptr;
  self->imgsz = 320;
  self->conf_thr = 0.25f;
  self->iou_thr  = 0.45f;
  self->ort_ready = FALSE;
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
