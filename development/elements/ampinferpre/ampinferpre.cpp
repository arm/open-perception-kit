#ifndef PACKAGE
#define PACKAGE "amp-elements"
#endif

#ifndef VERSION
#define VERSION "1.0"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>

#include <stdio.h>

#include "TensorMeta.h"

#include "uniflow/cpu_image_kernels.h"

// ------------------------------------------------

G_BEGIN_DECLS
#define GST_TYPE_AMPINFERPRE (gst_ampinferpre_get_type())
G_DECLARE_FINAL_TYPE(GstAmpInferPre, gst_ampinferpre, GST, AMPINFERPRE, GstVideoFilter)
struct _GstAmpInferPre { 
  GstVideoFilter parent_instance; 
};
G_END_DECLS

G_DEFINE_TYPE (GstAmpInferPre, gst_ampinferpre, GST_TYPE_VIDEO_FILTER)

// ------------------------------------------------

static GstFlowReturn gst_ampinferpre_transform_frame_ip(GstVideoFilter *vf, GstVideoFrame *frame)
{
  (void) vf;
//  (void) buffer;

return GST_FLOW_OK;

  //const int frameWidth = GST_VIDEO_FRAME_WIDTH(frame);
  //const int frameHeight = GST_VIDEO_FRAME_HEIGHT(frame);

  guint8 *rgb = (guint8*)GST_VIDEO_FRAME_PLANE_DATA(frame, 0);
  if (!rgb) return GST_FLOW_OK;

  GstMetaTensor* tensor = GstMetaTensorAttach(TensorType::Input, frame->buffer, 640 * 640 * 3 * 4);
  tensor->tensorType = TensorType::Input;
  tensor->valueType = uflw::ValueType::f32;

  GstMapInfo mem;
  if(true == GstMetaTensorLockData(tensor, &mem, true)) {



    GstMetaTensorUnlockData(tensor, &mem);
  }
    


  //GstMetaTensor* tensor = GstMetaTensorAttach(TensorType::Input, frame->buffer,
  //  uflw::ValueType::i8, 512, uflw::Quantization {}, uflw::Range {}
  //);
  
/*
  // preprocess
  auto input = resize_normalize_rgb_nn(rgb, W, H, self->imgsz);
  std::array<int64_t,4> ishape{{1,3,self->imgsz,self->imgsz}};
  Ort::Value in = Ort::Value::CreateTensor<float>(*self->mem_info,
    input.data(), (size_t)input.size(),
    ishape.data(), ishape.size());
*/

  return GST_FLOW_OK;
}

static void gst_ampinferpre_class_init(GstAmpInferPreClass* classPtr)
{
  GstVideoFilterClass *vfc = GST_VIDEO_FILTER_CLASS(classPtr);
  vfc->transform_frame_ip = gst_ampinferpre_transform_frame_ip;

  // simple ANY → ANY pad templates (should be tightened later)
  static GstStaticPadTemplate sinktempl = GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);
  static GstStaticPadTemplate srctempl  = GST_STATIC_PAD_TEMPLATE ("src",  GST_PAD_SRC,  GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

  /*
  static GstStaticPadTemplate sinktempl =
    GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-raw, format=(string)RGB"));

  static GstStaticPadTemplate srctempl  =
    GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-raw, format=(string)RGB"));
  */

  gst_element_class_add_static_pad_template(GST_ELEMENT_CLASS (classPtr), &sinktempl);
  gst_element_class_add_static_pad_template(GST_ELEMENT_CLASS (classPtr), &srctempl);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (classPtr),
    "AMP infer pre", "Filter/Effect/Video",
    "No-op skeleton element", "You <tamas.kulcsar@arm.com>");
}

static void gst_ampinferpre_init(GstAmpInferPre* self)
{
  // this element works in-place, but need this to access the pixel data and attach meta  (raw input tensow)
  gst_base_transform_set_in_place(GST_BASE_TRANSFORM (self), TRUE);

  // or true passthrough
  // gst_base_transform_set_passthrough(GST_BASE_TRANSFORM (self), TRUE);
}

static gboolean plugin_init(GstPlugin* plugin)
{
  return gst_element_register (plugin, "ampinferpre", GST_RANK_NONE, GST_TYPE_AMPINFERPRE);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  ampinferpre,
  "AMP infer pre",
  plugin_init,
  VERSION,
  "LGPL",
  PACKAGE,
  "https://example.com"
)
