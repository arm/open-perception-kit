#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#if defined(__arm__) || defined(__aarch64__)
using f16_type = __fp16;
#elif defined(__x86_64__)
using f16_type = _Float16;
#else
#error Unsupported architecture
#endif

namespace amp {

using Uint8 = uint8_t;
using Int8 = signed char;
using Float16 = f16_type;
using Float32 = float;
using Int64 = int64_t;

// tensor data type
enum class Tdt { Uint8, Int8, Float16, Float32, Int64 };

using ValuePointer = void *;

inline size_t getValueTypeByteSize(Tdt type) {
    switch (type) {
    case Tdt::Int8:
        return 1;
    case Tdt::Uint8:
        return 1;
    case Tdt::Float16:
        return 2;
    case Tdt::Float32:
        return 4;
    case Tdt::Int64:
        return 8;
    }
    return 0;
}

struct QuantizationArgs {
    float scale = 1.0f;
    float zeroPoint = 0.0f;
};

// ---

enum class TensorInOut { In, Out };

// represents the type of data stored in an input tensor
enum class DataKind {
    Unknown = 0,
    ImageRgbChw,  // RRRGGGBBB
    ImageRgbHwc,  // RGBRGBRGB
    ImageBgraHwc, // BGRABGRA
    ImageGray,

    Value,   // one scalar value (often used an an input tensor for some inference configuration)
    Vector2, // 2 scalar values
    Vector3, // 3 scalar values
    Vector4, // 4 scalar values

    AudioDUMMY,
    TextDUMMY,
};

inline bool isScalarDataKind(DataKind kind) {
    if (kind == DataKind::Value)
        return true;
    if (kind == DataKind::Vector2)
        return true;
    if (kind == DataKind::Vector3)
        return true;
    if (kind == DataKind::Vector4)
        return true;
    return false;
}

constexpr size_t MaxTensorCount = 4;
constexpr int64_t InvalidTensorIndex = 0xdead;

// Information about the inference itself
// sometimes these things are crucial for the parsing itself
struct ImageInferenceMetadata {
    // physical image dimensions the inference runs on
    size_t width = 0, height = 0;
    // the input tensor dimensions
    size_t modelWidth = 0, modelHeight = 0;
};

struct InferenceInfo {
    uint64_t parentUuid;
    std::string contentType;
    std::string modelFamily;
    ImageInferenceMetadata image;
};

struct PixelRect {
    size_t x = 0, y = 0, width = 0, height = 0;
};

} // namespace amp
