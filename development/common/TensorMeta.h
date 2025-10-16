#pragma once

#include <gst/gst.h>

#include "uniflow.h"

G_BEGIN_DECLS

enum class TensorType { Unknown = 0, Input, Output };

typedef struct _GstMetaTensor {

    GstMeta meta;

    gsize tensorByteSize;
    GstMemory* tensorData;

    TensorType tensorType;
    uflw::ValueType valueType;
    uflw::Quantization quantization;
    uflw::Range range;

} GstMetaTensor;

GType GstMetaTensor_get_type(void);
const GstMetaInfo* GstMetaTensor_get_info(void);

#define GST_META_TENSOR_TYPE (GstMetaTensor_get_type())
#define GST_META_TENSOR_INFO (GstMetaTensor_get_info())

GstMetaTensor* GstMetaTensorAttach(TensorType tensorType, GstBuffer *buf,  gsize tensorByteSize);
GstMetaTensor* GstMetaTensorGetAttached(GstBuffer* buf);
bool GstMetaTensorLockData(GstMetaTensor* meta, GstMapInfo* memMap, bool writable);
void GstMetaTensorUnlockData(GstMetaTensor* meta, GstMapInfo* memMap);

G_END_DECLS


