/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "TensorMeta.h"

#include <string.h>

static gboolean GstMetaTensor_init(GstMeta *meta, gpointer params, GstBuffer *buffer) {

    GstMetaTensor *m = (GstMetaTensor *)meta;

    m->tensorType = TensorType::Unknown;
    m->tensorData = nullptr;

    m->valueType = pek::Dtype::Uint8;

    m->quantization.zeroPoint = 0;
    m->quantization.scale = 1.0f;

    return TRUE;
}

static void GstMetaTensor_free(GstMeta *meta, GstBuffer *buffer) {
    GstMetaTensor *m = (GstMetaTensor *)meta;
    if (m->tensorData)
        gst_memory_unref(m->tensorData);
    m->tensorData = nullptr;
}

static gboolean GstMetaTensor_transform(
    GstBuffer *dest, GstMeta *meta, GstBuffer *src, GQuark type, gpointer data) {
    GstMetaTensor *m = (GstMetaTensor *)meta;
    GstMetaTensor *d = (GstMetaTensor *)gst_buffer_add_meta(dest, GST_META_TENSOR_INFO, NULL);

    d->valueType = m->valueType;
    d->quantization = m->quantization;

    d->tensorType = m->tensorType;
    d->tensorData = m->tensorData ? gst_memory_ref(m->tensorData) : NULL;

    return TRUE;
}

GType GstMetaTensor_get_type(void) {
    static GType type = 0;
    if (g_once_init_enter(&type)) {
        const char *api_name = "GstMetaTensorAPI";
        GType t = g_type_from_name(
            api_name); // refuse to register if this is not the first SO that uses the LIB
        if (!t) {
            static const gchar *tags[] = {"inference", "tensor", NULL};
            t = gst_meta_api_type_register(api_name, tags);
        }
        g_once_init_leave(&type, t);
    }
    return type;
}

const GstMetaInfo *GstMetaTensor_get_info(void) {
    static const GstMetaInfo *mi = NULL;
    if (g_once_init_enter(&mi)) {
        const GstMetaInfo *info = gst_meta_register(GST_META_TENSOR_TYPE,
                                                    "GstMetaTensor",
                                                    sizeof(GstMetaTensor),
                                                    GstMetaTensor_init,
                                                    GstMetaTensor_free,
                                                    GstMetaTensor_transform);
        g_once_init_leave(&mi, info);
    }
    return mi;
}

GstMetaTensor *GstMetaTensorGetAttached(GstBuffer *buf) {
    return (GstMetaTensor *)gst_buffer_get_meta(buf, GST_META_TENSOR_TYPE);
}

GstMetaTensor *GstMetaTensorAttach(TensorType tensorType, GstBuffer *buf, gsize tensorByteSize) {
    g_return_val_if_fail(GST_IS_BUFFER(buf), nullptr);
    g_return_val_if_fail(tensorByteSize > 0, nullptr);

    // Allocate meta structure
    GstMetaTensor *meta = (GstMetaTensor *)gst_buffer_add_meta(buf, GST_META_TENSOR_INFO, nullptr);
    if (!meta)
        return nullptr;

    // Fill the static metadata fields
    meta->tensorType = TensorType::Unknown;
    meta->tensorByteSize = tensorByteSize;
    meta->valueType = pek::Dtype::Int8;
    meta->quantization = pek::QuantizationArgs{};

    // Allocate writable GstMemory for the tensor
    meta->tensorData = gst_allocator_alloc(nullptr, meta->tensorByteSize, nullptr);
    if (!meta->tensorData) {
        GST_ERROR("Failed to allocate %" G_GSIZE_FORMAT " bytes for tensor in GstMetaTensor",
                  tensorByteSize);
        return nullptr;
    }

    return meta;
}

bool GstMetaTensorLockData(GstMetaTensor *meta, GstMapInfo *memMap, bool writable) {
    if (gst_memory_map(meta->tensorData, memMap, writable ? GST_MAP_WRITE : GST_MAP_READ))
        return true;
    return false;
}

void GstMetaTensorUnlockData(GstMetaTensor *meta, GstMapInfo *memMap) {
    gst_memory_unmap(meta->tensorData, memMap);
}
