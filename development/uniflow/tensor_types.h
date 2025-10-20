#pragma once

#include <cstdint>
#include <cstddef>

#include "public_types.h"

namespace uflw {




}

/*

struct InputSpec {
  Layout      layout;           // e.g. NCHW for YOLOv8; NHWC for many TFLite models
  ChannelOrder channels;        // RGB or BGR
  ValueType   dtype;            // f32, u8, i8
  Shape       shape;            // e.g. [1,3,320,320] or [1,320,320,3]
  bool        keep_aspect = true;
  bool        letterbox     = true;  // for YOLO-style inputs
  float       pad_value = 114.0f;    // common YOLO pad color
  // Normalization
  enum Norm { ZeroToOne, MinusOneToOne, MeanStd, None } norm = ZeroToOne;
  float mean[3] = {0,0,0}, std[3] = {1,1,1}; // used if norm==MeanStd
  // Quantization (if quantized input)
  bool         quantized = false;
  float        scale = 1.f;     // real = scale*(int - zeroPoint)
  int          zeroPoint = 0;
};

*/

/*

struct Tensor {
  void*  data;        // owned or external
  size_t bytes;
  ValueType dtype;
  Shape shape;
  Layout layout;
  // optional: deleter, strides
};

class TensorBuilder {
public:
  explicit TensorBuilder(const InputSpec& spec);

  // Images
  Tensor fromImage(const uint8_t* src, int srcW, int srcH, ChannelOrder srcOrder);

  // Audio (waveform -> model-ready)
  Tensor fromAudio(const float* mono16k, size_t samples);

  // Or incremental:
  TensorBuilder& resize(int W, int H, bool keep_aspect, bool letterbox, float pad_val);
  TensorBuilder& color(ChannelOrder src, ChannelOrder dst);
  TensorBuilder& toLayout(Layout dst);
  TensorBuilder& normalize(InputSpec::Norm n, const float mean[3], const float std[3]);
  TensorBuilder& quantize(ValueType to, float scale, int zp);

  Tensor buildInto(void* dst, size_t dstBytes); // write in-place (e.g., TFLM)
  Tensor build();                                // allocate & return owning Tensor
};

*/