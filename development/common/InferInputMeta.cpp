#include "InferInputMeta.h"

#include <string.h>

GType gst_infer_input_meta_api_get_type(void) {

    static GType type = 0;
    static const gchar *tags[] = { "inference", "tensor", NULL };
    if(g_once_init_enter (&type)) {
        GType t = gst_meta_api_type_register("GstInferInputMetaAPI", tags);
        g_once_init_leave (&type, t);
    }

    return type;
}

static gboolean gst_infer_input_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer) {

    GstInferInputMeta *m = (GstInferInputMeta *) meta;
    m->tensor = NULL;
    m->type = uf::Type::u8;
    m->ndims = 0;
    memset(m->dims, 0, sizeof (m->dims));
    memset(m->strides, 0, sizeof (m->strides));
    m->quantization.zeroPoint = 0;
    m->quantization.scale = 1.0f;
    m->range.type = uf::RangeType::Auto;
    
    return TRUE;
}

static void gst_infer_input_meta_free(GstMeta *meta, GstBuffer *buffer) {
    GstInferInputMeta *m = (GstInferInputMeta *) meta;
    if (m->tensor) g_bytes_unref (m->tensor);
    m->tensor = NULL;
}

static gboolean gst_infer_input_meta_transform(GstBuffer *dest, GstMeta *meta, GstBuffer *src, GQuark type, gpointer data)
{
    /* Copy meta 1:1 when the buffer is copied (e.g., downstream allocations) */
    GstInferInputMeta *m = (GstInferInputMeta *) meta;
    GstInferInputMeta *d = (GstInferInputMeta *)
        gst_buffer_add_meta (dest, GST_INFER_INPUT_META_INFO, NULL);

    d->type = m->type;
    d->ndims = m->ndims;
    memcpy (d->dims,    m->dims,    sizeof (m->dims));
    memcpy (d->strides, m->strides, sizeof (m->strides));
    d->quantization = m->quantization;
    d->range = m->range;

    if (m->tensor) d->tensor = g_bytes_ref (m->tensor);
    return TRUE;
}

const GstMetaInfo* gst_infer_input_meta_get_info(void)
{
    static const GstMetaInfo *meta_info = NULL;
    if (g_once_init_enter ((GstMetaInfo **) &meta_info)) {
    const GstMetaInfo *mi = gst_meta_register (
        GST_INFER_INPUT_META_API_TYPE,
        "GstInferInputMeta",
        sizeof (GstInferInputMeta),
        (GstMetaInitFunction) gst_infer_input_meta_init,
        (GstMetaFreeFunction) gst_infer_input_meta_free,
        gst_infer_input_meta_transform);
    g_once_init_leave ((GstMetaInfo **) &meta_info, (GstMetaInfo *) mi);
    }
    return meta_info;
}

GstInferInputMeta *
gst_buffer_add_infer_input_meta (GstBuffer *buf, GBytes *tensor, uf::Type type, 
                                 guint ndims, const guint32 *dims, const guint32 *strides,
                                 uf::Quantization quantization,
                                 uf::Range range) {

    g_return_val_if_fail (GST_IS_BUFFER (buf), NULL);
    g_return_val_if_fail (ndims <= 8, NULL);

    GstInferInputMeta* m = (GstInferInputMeta*)gst_buffer_add_meta (buf, GST_INFER_INPUT_META_INFO, NULL);

    m->type = type;
    m->ndims = ndims;
    for (guint i = 0; i < ndims; ++i) {
        m->dims[i] = dims ? dims[i] : 0;
        m->strides[i] = strides ? strides[i] : 0; /* 0 = tightly packed */
    }
    
    m->quantization = quantization;
    m->range = range;
    m->tensor = tensor ? g_bytes_ref (tensor) : NULL;

    return m;
}

GstInferInputMeta* gst_buffer_get_infer_input_meta(GstBuffer *buf)
{
    return (GstInferInputMeta*)gst_buffer_get_meta(buf, GST_INFER_INPUT_META_API_TYPE);
}

