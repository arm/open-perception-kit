#pragma once

#include <gst/gst.h>

#include "uniflow.h"

G_BEGIN_DECLS

/* Meta structure */
typedef struct _GstInferInputMeta {

    GstMeta meta;

    /* The tensor payload (OWNED ref). Using GBytes keeps meta trivially copyable. */
    GBytes* tensor; /* raw bytes, tightly packed unless strides used */

    uf::Type type;
    guint ndims; /* number of dimensions */
    guint32 dims[8]; /* up to 8D; set ndims accordingly */
    guint32 strides[8]; /* optional; 0 means tightly packed per dim */

    uf::Quantization quantization;
    uf::Range range;

} GstInferInputMeta;

// API + info accessors
GType gst_infer_input_meta_api_get_type(void);
const GstMetaInfo* gst_infer_input_meta_get_info(void);

// Attach
GstInferInputMeta*  gst_buffer_add_infer_input_meta(
    GstBuffer *buf,
    GBytes* tensor, /* takes a ref */
    uf::Type type,
    guint ndims,
    const guint32* dims, /* size ndims */
    const guint32* strides, /* size ndims or NULL */
    uf::Quantization quantization,
    uf::Range range);

// Convenience find
GstInferInputMeta*  gst_buffer_get_infer_input_meta (GstBuffer* buf);

#define GST_INFER_INPUT_META_API_TYPE (gst_infer_input_meta_api_get_type())
#define GST_INFER_INPUT_META_INFO (gst_infer_input_meta_get_info())

G_END_DECLS