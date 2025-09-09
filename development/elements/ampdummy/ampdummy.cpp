#ifndef PACKAGE
#define PACKAGE "amp-elements"
#endif

#ifndef VERSION
#define VERSION "1.0"
#endif

// ---

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include <stdio.h>

G_BEGIN_DECLS

#define GST_TYPE_AMPDUMMY (gst_ampdummy_get_type())
G_DECLARE_FINAL_TYPE (GstAmpDummy, gst_ampdummy, GST, AMPDUMMY, GstBaseTransform)

struct _GstAmpDummy { GstBaseTransform parent; };

G_END_DECLS

G_DEFINE_TYPE (GstAmpDummy, gst_ampdummy, GST_TYPE_BASE_TRANSFORM)

static GstFlowReturn
gst_ampdummy_transform_ip (GstBaseTransform *base, GstBuffer *buf)
{
  (void) base;
  (void) buf;

  printf("AMP DUMMY is processing..\n");

  return GST_FLOW_OK;
}

static void gst_ampdummy_class_init (GstAmpDummyClass *klass)
{
  GstBaseTransformClass *bt = GST_BASE_TRANSFORM_CLASS (klass);
  bt->transform_ip = gst_ampdummy_transform_ip;

  // Simple ANY → ANY pad templates (tighten later to video/x-raw etc.)
  static GstStaticPadTemplate sinktempl = GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);
  static GstStaticPadTemplate srctempl  = GST_STATIC_PAD_TEMPLATE ("src",  GST_PAD_SRC,  GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass), &sinktempl);
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass), &srctempl);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
    "AMP nothing (dummy)", "Filter/Effect/Video",
    "No-op skeleton element", "You <tamas.kulcsar@arm.com>");
}

static void gst_ampdummy_init (GstAmpDummy *self)
{
  // sign this element works in-place
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (self), TRUE);

  // If it truly never changes data/caps, you can also mark passthrough:
  // gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (self), TRUE);
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "ampdummy", GST_RANK_NONE, GST_TYPE_AMPDUMMY);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  ampdummy,
  "AMP dummy",
  plugin_init,
  VERSION,
  "LGPL",
  PACKAGE,
  "https://example.com"
)