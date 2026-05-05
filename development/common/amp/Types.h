/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "amp/Color.h"

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

struct MeanStd {
    static bool isDefaultMean(const amp::Colorf &value) {
        if (value.r != 0.0f || value.g != 0.0f || value.b != 0.0f)
            return false;
        return true;
    }

    static bool isDefaultStd(const amp::Colorf &value) {
        if (value.r != 1.0f || value.g != 1.0f || value.b != 1.0f)
            return false;
        return true;
    }
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

    RawTensorData, // raw (sometimes quantized) tensor data, e.g. output-tensor-content after
                   // inference

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

inline bool isImageDataKind(DataKind kind) {
    if (kind == DataKind::ImageRgbChw)
        return true;
    if (kind == DataKind::ImageRgbHwc)
        return true;
    if (kind == DataKind::ImageBgraHwc)
        return true;
    if (kind == DataKind::ImageGray)
        return true;
    return false;
}

constexpr size_t MaxTensorCount = 8;
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
    std::string inferElementId;
    ImageInferenceMetadata image;
};

struct PixelRect {
    size_t x = 0, y = 0, width = 0, height = 0;

    bool isEmpty() const {
        return width == 0 || height == 0;
    }

    bool fitsWithin(size_t surfaceWidth, size_t surfaceHeight) const {
        return x <= surfaceWidth && y <= surfaceHeight && width <= surfaceWidth - x &&
               height <= surfaceHeight - y;
    }
};

struct ImageLayoutDesc {
    uint8_t *data = nullptr;
    size_t byteCount = 0;

    size_t surfaceWidth = 0;
    size_t surfaceHeight = 0;
    size_t surfaceStride = 0; // in bytes, NOT USED YET, we assume tightly packed for now

    PixelRect rect;

    DataKind kind = DataKind::Unknown;
    amp::Tdt type = amp::Tdt::Float32;

    amp::Colorf mean = {0.0f, 0.0f, 0.0f, 0.0f};
    amp::Colorf std = {1.0f, 1.0f, 1.0f, 1.0f};

    size_t getChannelCount() const {
        switch (kind) {
        case DataKind::ImageRgbChw:
            return 3;
        case DataKind::ImageRgbHwc:
            return 3;
        case DataKind::ImageBgraHwc:
            return 4;
        case DataKind::ImageGray:
            return 1;
        default:
            return 0;
        }
    }

    bool hasKnownImageStorage() const {
        return getChannelCount() != 0;
    }

    bool rectIsFullSurface() const {
        return rect.x == 0 && rect.y == 0 && rect.width == surfaceWidth &&
               rect.height == surfaceHeight;
    }

    size_t getMinimumByteCountForFullSurface() const {
        const size_t channelCount = getChannelCount();
        const size_t valueSize = amp::getValueTypeByteSize(type);
        if (channelCount == 0 || valueSize == 0)
            return 0;
        return surfaceWidth * surfaceHeight * channelCount * valueSize;
    }

    bool hasByteCountForFullSurface() const {
        const size_t minimumByteCount = getMinimumByteCountForFullSurface();
        if (minimumByteCount == 0)
            return false;
        return byteCount >= minimumByteCount;
    }
};

enum class Sampling { Nearest, Linear /* not supported yet */ };

struct TensorFeedback {
    enum class Mode { Copy };

    size_t fromOutputTensorIndex = 0;
    size_t toInputTensorIndex = 0;
    Mode mode = Mode::Copy;
};

} // namespace amp
