#include "InferInputMeta.h"

#include <string.h>

static gboolean GstMetaInferInput_init(GstMeta *meta, gpointer params, GstBuffer *buffer) {

    GstMetaInferInput* m = (GstMetaInferInput*)meta;
    m->tensor = NULL;
    m->type = uf::Type::u8;
    m->quantization.zeroPoint = 0;
    m->quantization.scale = 1.0f;
    m->range.type = uf::RangeType::Auto;
    
    return TRUE;
}

static void GstMetaInferInput_free(GstMeta *meta, GstBuffer *buffer) {
    GstMetaInferInput* m = (GstMetaInferInput*)meta;
    if (m->tensor) g_bytes_unref (m->tensor);
    m->tensor = NULL;
}

static gboolean GstMetaInferInput_transform(GstBuffer* dest, GstMeta* meta, GstBuffer* src, GQuark type, gpointer data)
{
    GstMetaInferInput* m = (GstMetaInferInput*)meta;
    GstMetaInferInput* d = (GstMetaInferInput*)gst_buffer_add_meta(dest, GST_META_INFER_INPUT_INFO, NULL);

    d->type = m->type;
    d->quantization = m->quantization;
    d->range = m->range;

    if (m->tensor) d->tensor = g_bytes_ref(m->tensor);
    return TRUE;
}

GType GstMetaInferInput_get_type (void) {
    static gsize type_id = 0;
    static const gchar *tags[] = { "inference", "tensor", NULL };
    if (g_once_init_enter(&type_id)) {
        GType t = gst_meta_api_type_register("GstMetaInferInputAPI", tags);
        g_once_init_leave(&type_id, (gsize)t);
    }
    return (GType)type_id;
}

const GstMetaInfo* GstMetaInferInput_get_info(void) {
    static const GstMetaInfo *mi = NULL;
    if (g_once_init_enter(&mi)) {
        const GstMetaInfo *info = gst_meta_register(
            GST_META_INFER_INPUT_TYPE,
            "GstMetaInferInput",
            sizeof (GstMetaInferInput),
            GstMetaInferInput_init,
            GstMetaInferInput_free,
            GstMetaInferInput_transform);
        g_once_init_leave(&mi, info);
    }
    return mi;
}

GstMetaInferInput* GstMetaInferInput_get_attached(GstBuffer *buf)
{
    return (GstMetaInferInput*)gst_buffer_get_meta(buf, GST_META_INFER_INPUT_TYPE);
}


/*


GstInferInputMeta *
gst_buffer_add_infer_input_meta (GstBuffer *buf, GBytes *tensor, uf::Type type, 
                                 guint ndims, const guint32 *dims, const guint32 *strides,
                                 uf::Quantization quantization,
                                 uf::Range range) {

    g_return_val_if_fail (GST_IS_BUFFER (buf), NULL);
    g_return_val_if_fail (ndims <= 8, NULL);

    GstInferInputMeta* m = (GstInferInputMeta*)gst_buffer_add_meta(buf, GST_INFER_INPUT_META_INFO, NULL);

    m->type = type;
    m->ndims = ndims;
    for (guint i = 0; i < ndims; ++i) {
        m->dims[i] = dims ? dims[i] : 0;
        m->strides[i] = strides ? strides[i] : 0;
    }
    
    m->quantization = quantization;
    m->range = range;
    m->tensor = tensor ? g_bytes_ref (tensor) : NULL;

    return m;
}
*/


