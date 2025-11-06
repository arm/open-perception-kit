
#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>

#include <onnxruntime_cxx_api.h>

#include <stdio.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "OnnxTools.h"

#include "uniflow/public_types.h"
#include "uniflow/yolo_like_parser.h"

static int yoloSquareSize = 0;

// int8 model for embedded use, the float32 version is loaded from file
//extern unsigned int yolov8n_int8_onnx_len;
//extern unsigned char yolov8n_int8_onnx[];

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
};

G_END_DECLS

G_DEFINE_TYPE (GstAmpInfer, gst_ampinfer, GST_TYPE_VIDEO_FILTER)

// ---------------- utils ----------------

static inline float iou_xyxy(const float a[4], const float b[4]) {
  const float x1 = std::max(a[0], b[0]), y1 = std::max(a[1], b[1]);
  const float x2 = std::min(a[2], b[2]), y2 = std::min(a[3], b[3]);
  const float iw = std::max(0.f, x2 - x1), ih = std::max(0.f, y2 - y1);
  const float inter = iw * ih;
  const float areaA = std::max(0.f, a[2]-a[0]) * std::max(0.f, a[3]-a[1]);
  const float areaB = std::max(0.f, b[2]-b[0]) * std::max(0.f, b[3]-b[1]);
  const float uni = areaA + areaB - inter;
  return uni > 0.f ? inter / uni : 0.f;
}

static std::vector<float> resize_normalize_rgb_nn(const guint8* src, int W, int H, int S) {
  // nearest-neighbor to NxCxSxS float NCHW (N=1)
  std::vector<float> out(1 * 3 * S * S);
  for (int y=0; y<S; ++y) {
    const int sy = y * H / S;
    for (int x=0; x<S; ++x) {
      const int sx = x * W / S;
      const guint8* p = src + (sy * W + sx) * 3;
      const size_t o0 = (0 * 3 + 0) * S * S + y * S + x;
      const size_t o1 = (0 * 3 + 1) * S * S + y * S + x;
      const size_t o2 = (0 * 3 + 2) * S * S + y * S + x;
      out[o0] = p[0] / 255.f;
      out[o1] = p[1] / 255.f;
      out[o2] = p[2] / 255.f;
    }
  }
  return out;
}

struct Det { float x1,y1,x2,y2,conf; int cls; };

static std::vector<Det> yolov8_like_post(const float* data, const std::vector<int64_t>& dims,
                 int origW, int origH, float conf_thr, float iou_thr) {
  // accept [1, N, 84] or [1, 84, N]
  int N = 0; bool transposed = false;
  if (dims.size()==3 && dims[0]==1 && dims[2]==84) {
    N = (int)dims[1]; // [1,N,84]
  } else if (dims.size()==3 && dims[0]==1 && dims[1]==84) {
    N = (int)dims[2]; // [1,84,N] -> transpose view
    transposed = true;
  } else {
    return {};
  }

  std::vector<Det> dets;
  dets.reserve(64);

  for (int i=0;i<N;++i) {
    const float* row;
    if (!transposed) {
      row = data + i*84;
    } else {
      static float tmp[84];
      for (int j=0;j<84;++j) tmp[j] = data[j*N + i];
      row = tmp;
    }

    const float cx=row[0], cy=row[1], w=row[2], h=row[3];

    int best_c = -1; float best_s = 0.f;
    for (int c=4;c<84;++c) if (row[c] > best_s) { best_s = row[c]; best_c = c-4; }
    if (best_s < conf_thr) continue;

    const float x1 = (cx - w*0.5f) * origW;
    const float y1 = (cy - h*0.5f) * origH;
    const float x2 = (cx + w*0.5f) * origW;
    const float y2 = (cy + h*0.5f) * origH;

    dets.push_back({x1,y1,x2,y2,best_s,best_c});
  }

  // NMS
  std::sort(dets.begin(), dets.end(), [](const Det&a,const Det&b){return a.conf>b.conf;});
  std::vector<Det> out;
  std::vector<char> sup(dets.size(), 0);
  for (size_t i=0;i<dets.size();++i) {
    if (sup[i]) continue;
    out.push_back(dets[i]);
    for (size_t j=i+1;j<dets.size();++j) {
      if (sup[j]) continue;
      float A[4]={dets[i].x1,dets[i].y1,dets[i].x2,dets[i].y2};
      float B[4]={dets[j].x1,dets[j].y1,dets[j].x2,dets[j].y2};
      if (iou_xyxy(A,B) > iou_thr) sup[j]=1;
    }
    if (out.size() >= 50) break;
  }
  return out;
}

// ---------------- Gst virtuals ----------------

static gboolean gst_ampinfer_start (GstBaseTransform *b) {
  auto *self = (GstAmpInfer*) b;

  try {
  self->session_opts = new Ort::SessionOptions();
  self->session_opts->SetIntraOpNumThreads(1);

  self->env = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "ampinfer");
  self->mem_info = new Ort::MemoryInfo(Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU));
  
    self->session = new Ort::Session(*self->env, self->model_path, *self->session_opts);

    // cache I/O names (works with ONNX Runtime 1.18+)
    Ort::AllocatorWithDefaultOptions alloc;
    const size_t ni = self->session->GetInputCount();
    const size_t no = self->session->GetOutputCount();

    self->input_names.clear();
    self->output_names.clear();
    for (size_t i=0;i<ni;++i) {
      auto s = self->session->GetInputNameAllocated(i, alloc);
      self->input_names.push_back(strdup(s.get()));
    }
    for (size_t i=0;i<no;++i) {
      auto s = self->session->GetOutputNameAllocated(i, alloc);
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

static inline void drawBox(guint8* rgb, int framew, int frameh, float x0, float y0, float x1, float y1) {

  int _x0 = (int)x0;
  int _y0 = (int)y0;
  int _x1 = (int)x1;
  int _y1 = (int)y1;

  for(int x = _x0; x <= _x1; x++) {
        rgb[3 * (_y0 * framew + x) + 0] = 0xff;
        rgb[3 * (_y0 * framew + x) + 1] = 0xff;
        rgb[3 * (_y0 * framew + x) + 2] = 0xff;

        rgb[3 * (_y1 * framew + x) + 0] = 0xff;
        rgb[3 * (_y1 * framew + x) + 1] = 0xff;
        rgb[3 * (_y1 * framew + x) + 2] = 0xff;
  }

  for(int y = _y0; y <= _y1; y++) {
      rgb[3 * (y * framew + _x0) + 0] = 0xff;
      rgb[3 * (y * framew + _x0) + 1] = 0xff;
      rgb[3 * (y * framew + _x0) + 2] = 0xff;

      rgb[3 * (y * framew + _x1) + 0] = 0xff;
      rgb[3 * (y * framew + _x1) + 1] = 0xff;
      rgb[3 * (y * framew + _x1) + 2] = 0xff;
  }
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

static float IoU(const Det& a, const Det& b){
  float xx1 = std::max(a.x1,b.x1), yy1=std::max(a.y1,b.y1);
  float xx2 = std::min(a.x2,b.x2), yy2=std::min(a.y2,b.y2);
  float w = std::max(0.f, xx2-xx1), h = std::max(0.f, yy2-yy1);
  float inter = w*h, uni=(a.x2-a.x1)*(a.y2-a.y1)+(b.x2-b.x1)*(b.y2-b.y1)-inter;
  return uni>0? inter/uni : 0.f;
}
static void NMS(std::vector<Det>& d, float iou_thr){
  std::sort(d.begin(), d.end(), [](auto&a,auto&b){return a.conf>b.conf;});
  std::vector<char> sup(d.size());
  std::vector<Det> keep; keep.reserve(d.size());
  for(size_t i=0;i<d.size();++i){
    if(sup[i]) continue; 
    keep.push_back(d[i]);
    for(size_t j=i+1;j<d.size();++j) if(!sup[j] && IoU(d[i],d[j])>iou_thr) sup[j]=1;
  }
  d.swap(keep);
}

static inline float clampf(float v,float lo,float hi){return std::max(lo,std::min(v,hi));}

// --------------------------------------------------------------

#include <fstream>

static size_t elem_size(ONNXTensorElementDataType t) {
  switch (t) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:   return sizeof(float);
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:   return sizeof(uint8_t);
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:    return sizeof(int8_t);
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16:  return sizeof(uint16_t);
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16:   return sizeof(int16_t);
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:   return sizeof(int32_t);
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:   return sizeof(int64_t);
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:    return sizeof(uint8_t);
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16: return 2;            // IEEE 754 half
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE:  return sizeof(double);
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32:  return sizeof(uint32_t);
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64:  return sizeof(uint64_t);
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16:return 2;
    default: return 0; // strings / complex / undefined handled separately
  }
}

static GstFlowReturn gst_ampinfer_transform_frame_ip (GstVideoFilter *vf, GstVideoFrame *frame)
{
  auto *self = (GstAmpInfer*) vf;
  if (!self->ort_ready) return GST_FLOW_OK;

  if(yoloSquareSize == 0) {
    yoloSquareSize = (int)self->imgsz;
  }

  const int W = GST_VIDEO_FRAME_WIDTH(frame);
  const int H = GST_VIDEO_FRAME_HEIGHT(frame);

  guint8 *rgb = (guint8*) GST_VIDEO_FRAME_PLANE_DATA(frame, 0);
  if (!rgb) return GST_FLOW_OK;

  // preprocess
  //printf("%d %d -> %d %d\n", W, H, self->imgsz, yoloSquareSize);
  auto input = resize_normalize_rgb_nn(rgb, W, H, self->imgsz);
  std::array<int64_t,4> ishape{{1,3,self->imgsz,self->imgsz}};
  Ort::Value in = Ort::Value::CreateTensor<float>(*self->mem_info,
    input.data(), (size_t)input.size(),
    ishape.data(), ishape.size());

  // run
  std::vector<Ort::Value> out = self->session->Run(
    Ort::RunOptions{nullptr},
    (const char* const*)self->input_names.data(), &in, 1,
    (const char* const*)self->output_names.data(), self->output_names.size());

  // --------------------------------------------------------------

  uflw::ConfidenceLabelBox resultBoxes[32];

  OnnxOutputTensor tensor(out);
  
  uflw::YoloLikeParser::Config config;

  
  uflw::YoloLikeParser::parse(
    (void*)tensor.getRawData(), tensor.getByteSize(), 
    uflw::ValueType::f32, config,
    resultBoxes, 32);


  // --------------------------------------------------------------

  // model & frame sizes
  const int IMG = self->imgsz; // square inference size (e.g. 640)
  const float sx = float(W)/IMG, sy = float(H)/IMG; // scale back to frame

  Ort::Value& v = out.at(0);
  auto info  = v.GetTensorTypeAndShapeInfo();

  std::vector<int64_t> dims = info.GetShape();
  float* p = v.GetTensorMutableData<float>();

  ONNXTensorElementDataType tensorType = info.GetElementType();
  if( tensorType == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) printf("this is float\n");

  int valueCount = 1;
  for(const auto& a : dims) {
    printf("%d ", a);
    valueCount *= a;
  }
  printf("\n");
  int outTensorByteCount = valueCount * 4;
  void* tensorBytes = p;
  printf("outTensorByteCount: %d\n", outTensorByteCount);


  // Detect layout
  // A: [1,84,N] => C=84, N=dims[2]
  // B: [1,N,84] => N=dims[1], C=84
  int64_t N=-1, C=-1; bool col_first=false;
  if(dims.size()==3 && dims[0]==1){
    if(dims[1]==84){ C=84; N=dims[2]; col_first=true;  } // [1,84,N]
    else if(dims[2]==84){ C=84; N=dims[1]; col_first=false; } // [1,N,84]
  }
  if(N <= 0 || C != 84) { 
    /* unexpected shape */ 
  }

  const float conf_thr = 0.25f; //self->conf; // e.g., 0.25
  const float iou_thr  = 0.45f; //self->iou; // e.g., 0.45
  std::vector<Det> dets; dets.reserve((size_t)N);

  // Iterate candidates
  for (int64_t i=0; i<N; ++i) {
    const float* row = col_first ? (p + i) : (p + i*84);
    // read box (cx,cy,w,h)
    float cx, cy, bw, bh;
    if (col_first) { // [84 x N]: stride = N
      const int64_t s = N;
      cx=row[0*s]; cy=row[1*s]; bw=row[2*s]; bh=row[3*s];
      // classes start at 4*s
      int best=-1; float bestp=0.f;
      for (int c=0;c<80;++c){ float sc=row[(4+c)*s]; if(sc>bestp){bestp=sc; best=c;} }
      if (bestp < conf_thr) continue;
      // convert to xyxy in model space
      float x1 = cx - bw/2.f, y1 = cy - bh/2.f, x2 = cx + bw/2.f, y2 = cy + bh/2.f;
      // scale to frame
      Det d{ x1*sx, y1*sy, x2*sx, y2*sy, bestp, best };
      d.x1=clampf(d.x1,0,W-1); d.y1=clampf(d.y1,0,H-1);
      d.x2=clampf(d.x2,0,W-1); d.y2=clampf(d.y2,0,H-1);
      dets.push_back(d);
    } else { // [N x 84]: contiguous row of 84
      cx=row[0]; cy=row[1]; bw=row[2]; bh=row[3];
      int best=-1; float bestp=0.f;
      for (int c=0;c<80;++c){ float sc=row[4+c]; if(sc>bestp){bestp=sc; best=c;} }
      if (bestp < conf_thr) continue;
      float x1 = cx - bw/2.f, y1 = cy - bh/2.f, x2 = cx + bw/2.f, y2 = cy + bh/2.f;
      Det d{ x1*sx, y1*sy, x2*sx, y2*sy, bestp, best };
      d.x1=clampf(d.x1,0,W-1); d.y1=clampf(d.y1,0,H-1);
      d.x2=clampf(d.x2,0,W-1); d.y2=clampf(d.y2,0,H-1);
      dets.push_back(d);
    }
  }

  NMS(dets, iou_thr);

  for(const auto& a : dets) {
    drawBox(rgb, W, H, a.x1, a.y1, a.x2, a.y2);
  }

  static int counter = 1000;
  if(dets.size() > 0) {
    std::string s, fileName;

    fileName = std::string("/work/temp/dump/" + std::to_string(counter));
    counter++;

//struct Det { float x1,y1,x2,y2,conf; int cls; };
    s += std::to_string(dets[0].x1) + " ";
    s += std::to_string(dets[0].y1) + " ";
    s += std::to_string(dets[0].x2) + " ";
    s += std::to_string(dets[0].y2) + " conf ";
    s += std::to_string(dets[0].conf) + " class ";
    s += std::to_string(dets[0].cls) + "\n";
    printf("%s\n", s.c_str());
  
    //int outTensorByteCount = valueCount * 4;
    //void* tensorBytes = p;
    //write_numeric_tensor_raw(out, fileName + ".dump");

    /*
    FILE* f = fopen((fileName + ".dump").c_str(), "wb");
    if(f) {
      fwrite(tensorBytes, 1, outTensorByteCount, f);
      fclose(f);
    }*/

    {

      for(int i = 0; i < 16; i++) {
        printf("%f ", ((float*)tensorBytes)[i]);
      }
      printf("\n");
      printf("\n");

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
  static GstStaticPadTemplate sink_t =
    GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-raw, format=(string)RGB"));
  static GstStaticPadTemplate src_t  =
    GST_STATIC_PAD_TEMPLATE ("src",  GST_PAD_SRC,  GST_PAD_ALWAYS,
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
