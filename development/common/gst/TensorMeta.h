/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include <gst/gst.h>

#include "opk/Types.h"

G_BEGIN_DECLS

enum class TensorType { Unknown = 0, Input, Output };

typedef struct _GstMetaTensor {

    GstMeta meta;

    gsize tensorByteSize;
    GstMemory *tensorData;

    TensorType tensorType;
    opk::Dtype valueType;
    opk::QuantizationArgs quantization;

} GstMetaTensor;

GType GstMetaTensor_get_type(void);
const GstMetaInfo *GstMetaTensor_get_info(void);

#define GST_META_TENSOR_TYPE (GstMetaTensor_get_type())
#define GST_META_TENSOR_INFO (GstMetaTensor_get_info())

GstMetaTensor *GstMetaTensorAttach(TensorType tensorType, GstBuffer *buf, gsize tensorByteSize);
GstMetaTensor *GstMetaTensorGetAttached(GstBuffer *buf);
bool GstMetaTensorLockData(GstMetaTensor *meta, GstMapInfo *memMap, bool writable);
void GstMetaTensorUnlockData(GstMetaTensor *meta, GstMapInfo *memMap);

G_END_DECLS
