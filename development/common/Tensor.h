#pragma once

#include <gst/gst.h>

enum class GstTensorDataType { 
  int8,
  uint8,
  fp16,
  fp32
};

/*

struct GstTensorData {
  GstMeta meta;
  GstTensorDType dtype;
  float scale; int zero_point;

  GstMemory *mem;
  void *data;
  gsize size;
};

GType gst_tensor_meta_api_get_type(void);
const GstMetaInfo * gst_tensor_meta_get_info(void);

#define GST_TENSOR_META_API_TYPE(gst_tensor_meta_api_get_type())
#define GST_TENSOR_META_INFO(gst_tensor_meta_get_info())

static inline GstTensorMeta* gst_buffer_add_tensor_meta (GstBuffer *buf) {
  return (GstTensorMeta *) gst_buffer_add_meta (buf, GST_TENSOR_META_INFO, NULL);
};

*/


