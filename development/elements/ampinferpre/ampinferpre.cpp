#ifndef PACKAGE
#define PACKAGE "amp-elements"
#endif

#ifndef VERSION
#define VERSION "1.0"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <stdio.h>

// ------------------------------------------------

G_BEGIN_DECLS
#define GST_TYPE_AMPINFERPRE (gst_ampinferpre_get_type())
G_DECLARE_FINAL_TYPE(GstAmpInferPre, gst_ampinferpre, GST, AMPINFERPRE, GstBaseTransform)
struct _GstAmpInferPre { 
  GstBaseTransform parent; 
};
G_END_DECLS

G_DEFINE_TYPE (GstAmpInferPre, gst_ampinferpre, GST_TYPE_BASE_TRANSFORM)

// ------------------------------------------------

static GstFlowReturn gst_ampinferpre_transform_ip(GstBaseTransform* base, GstBuffer* buffer)
{
  (void) base;
  (void) buffer;

  printf("AMP INFER PRE is processing..\n");

  return GST_FLOW_OK;
}

static void gst_ampinferpre_class_init(GstAmpInferPreClass* classPtr)
{
  GstBaseTransformClass *bt = GST_BASE_TRANSFORM_CLASS(classPtr);
  bt->transform_ip = gst_ampinferpre_transform_ip;

  // simple ANY → ANY pad templates (should be tightened later)
  static GstStaticPadTemplate sinktempl = GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);
  static GstStaticPadTemplate srctempl  = GST_STATIC_PAD_TEMPLATE ("src",  GST_PAD_SRC,  GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

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
