#include "Tensor.h"

int test_get() { return -2; } 

#ifdef SKIP

static gboolean tensor_meta_init (GstMeta *meta, gpointer params, GstBuffer *buffer) {
  GstTensorMeta *m = (GstTensorMeta *) meta;
  m->dtype = GST_TENSOR_INT8;
  m->dims[0]=m->dims[1]=m->dims[2]=m->dims[3]=0;
  strcpy(m->layout, "NHWC");
  m->scale = 1.f; m->zero_point = 0;
  m->mem = NULL; m->data = NULL; m->size = 0;
  m->schema_version = 1;
  return TRUE;
}

static gboolean tensor_meta_transform (GstBuffer *dest, GstMeta *meta,
                                       GstBuffer *src, GQuark type, gpointer data) {
  // Ensure meta is copied when buffers are copied/transformed
  const GstTensorMeta *s = (const GstTensorMeta *) meta;
  GstTensorMeta *d = gst_buffer_add_tensor_meta (dest);
  *d = *s; // shallow copy of POD fields

  if (s->mem) {
    // re-ref memory onto the destination buffer
    d->mem = gst_memory_ref (s->mem);
    gst_buffer_append_memory (dest, d->mem);
  } else if (s->data && s->size) {
    // if you manage raw pointer, duplicate or share carefully
    // (prefer GstMemory to avoid lifetime pitfalls)
  }
  return TRUE;
}

static void tensor_meta_free (GstMeta *meta, GstBuffer *buffer) {
  GstTensorMeta *m = (GstTensorMeta *) meta;
  if (m->mem) gst_memory_unref (m->mem);
  // if (m->data) free(m->data); // only if you allocated it
}

GType gst_tensor_meta_api_get_type(void) {
  static volatile GType type;
  static const gchar *tags[] = { "tensor", NULL };
  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstTensorMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

const GstMetaInfo * gst_tensor_meta_get_info(void) {
  static const GstMetaInfo *info = NULL;
  if (g_once_init_enter ((GstMetaInfo **) &info)) {
    const GstMetaInfo *mi = gst_meta_register (
      GST_TENSOR_META_API_TYPE, "GstTensorMeta",
      sizeof (GstTensorMeta),
      tensor_meta_init, tensor_meta_free, tensor_meta_transform);
    g_once_init_leave ((GstMetaInfo **) &info, (GstMetaInfo *) mi);
  }
  return info;
}
#endif

