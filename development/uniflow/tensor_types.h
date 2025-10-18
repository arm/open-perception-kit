#pragma once

#include <cstdint>
#include <cstddef>

namespace uflw {

    // Types that build up the tensors.
    // C++ types to use as tensor values and enum values to identify tensor types.
    using u8 = unsigned char;
    using i8 = signed char;
    using f16 = unsigned short;
    using f32 = float;

    enum class ValueType { 
        u8, 
        i8, 
        f16, 
        f32 
    };

    using ValuePointer = void*;

    // Shape that holds value count for each dimensions.
    struct Shape {

        explicit Shape(int d0 = 0, int d1 = 0, int d2 = 0, int d3 = 0, int d4 = 0, int d5 = 0, int d6 = 0, int d7 = 0) {
            if(d0 > 0) { valueCount[0] = d0; dimensionCount = 1; }  
            if(d1 > 0) { valueCount[1] = d1; dimensionCount = 2; }  
            if(d2 > 0) { valueCount[2] = d2; dimensionCount = 3; }  
            if(d3 > 0) { valueCount[3] = d3; dimensionCount = 4; }  
            if(d4 > 0) { valueCount[4] = d4; dimensionCount = 5; }  
            if(d5 > 0) { valueCount[5] = d5; dimensionCount = 6; }  
            if(d6 > 0) { valueCount[6] = d6; dimensionCount = 7; }  
            if(d7 > 0) { valueCount[7] = d7; dimensionCount = 8; }  
        }

        uint8_t valueCount[8] = { 0 };
        size_t dimensionCount = 0;
    };

    // ---

    // Arguments to for value conversions to generate input tensor.
    // Original min/max is the minimum and maximum value of the tensor values used during training.
    struct QuantizationArgs { 
        float trainRangeMin = -1.0f, trainRangeMax = -1.0f;
        float scale = 1.0f; 
        int zeroPoint = 0; 
    };

    // Training range is used to normalize values (e.g. RGB U8 values) before quantize them.
    // The input values are normalized into the "min-max" range when Range type is set to Exact.
    // They are normalized to 0-1 or -1-1 if set to Auto see updateRange() for details.
    void updateRange(QuantizationArgs& qantArgs, ValueType valueType);

    // Helper functions to convert:
    // Tensor type -> Normalized 0-1 float
    // Normalized 0-1 float -> tensor type
    float Float01_From_f16(f16 h);
    f16 Float01_Into_f16(float f);
    float Float01_From_f32(f32 v);
    f32 Float01_Into_f32(float v);
    float Float01_From_u8(u8 v);
    u8 Float01_Into_u8(float v);
    float Float01_From_i8(i8 v);
    i8 Float01_Into_i8(float v);

    // Raw data byte layout
    enum class RawDataFormat {
        Rgb8,
        Rgbf32,
        Gray8,
        Grayf32,
        Jpeg,
        MonoPcm8,
        MonoPcm16
    };

    // Data layout
    // N=batch, C=channels, H/W=height/width
    // T=time, F=frames (spectrogram)
    enum class TensorLayout  {
        CHW, // [R...][G...][B...]
        HWC, // [R,G,B][R,G,B][R,G,B] ...

        NCHW, NHWC, // batch processing (support is TBD)
        
        // N=0: [t0: C0,C1,C2,...] [t1: C0,C1,C2,...] [t2: C0,C1,C2,...]
        // N=1: [t0: C0,C1,C2,...] [t1: C0,C1,C2,...] ...
        NTC, 
        
        // N=0: [C0: F0,F1,F2,...]
        //      [C1: F0,F1,F2,...]
        // N=1: [C0: F0,F1,F2,...]
        NCF
    };

    // Helper, should be removed
    struct DetectionBox {
        float x1, y1, x2, y2;
        float score;
        int label;
    };

    // Underlaying engine, to handle their quirks
    enum class InferenceEngine {
        OnnxRt,
        Tflm,
        ExecuTorch
    };

    size_t measeureTensorByteSize(ValueType valueType) {}
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