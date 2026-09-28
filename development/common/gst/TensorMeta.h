/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
