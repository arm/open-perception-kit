#pragma once

#include <gst/gst.h>

#include "uniflow.h"

G_BEGIN_DECLS

typedef struct GstMetaInferInput {

    GstMeta meta;
    GBytes* tensor;

    uf::Type type;
    uf::Quantization quantization;
    uf::Range range;

} GstMetaInferInput;

GType GstMetaInferInput_get_type(void);
const GstMetaInfo* GstMetaInferInput_get_info(void);
GstMetaInferInput* GstMetaInferInput_get_attached(GstBuffer* buf);

#define GST_META_INFER_INPUT_TYPE (GstMetaInferInput_get_type())
#define GST_META_INFER_INPUT_INFO (GstMetaInferInput_get_info())

G_END_DECLS


