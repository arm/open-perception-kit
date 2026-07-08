/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

#include "pek/Color.h"

namespace pek {

#if defined(__arm__) || defined(__aarch64__)
using f16_type = __fp16;
#elif defined(__x86_64__)
using f16_type = _Float16;
#else
#error Unsupported architecture
#endif

/// @brief Alias for uint8_t tensor element type.
using Uint8 = uint8_t;
/// @brief Alias for signed char tensor element type.
using Int8 = signed char;
/// @brief Alias for 16-bit float tensor element type.
using Float16 = f16_type;
/// @brief Alias for 32-bit float tensor element type.
using Float32 = float;
/// @brief Alias for 64-bit signed integer tensor element type.
using Int64 = int64_t;

/**
 * @brief Tensor element data type.
 */
enum class Dtype { Uint8, Int8, Float16, Float32, Int64 };

/// @brief Type-erased pointer to tensor data.
using ValuePointer = void *;

/**
 * @brief Returns the byte size of a single element of the given data type.
 * @param type Tensor element data type.
 * @return Byte size of one element.
 */
inline size_t getValueTypeByteSize(Dtype type) {
    switch (type) {
    case Dtype::Int8:
    case Dtype::Uint8:
        return 1;
    case Dtype::Float16:
        return 2;
    case Dtype::Float32:
        return 4;
    case Dtype::Int64:
        return 8;
    }
    throw std::runtime_error("Unknown Dtype in getValueTypeByteSize()");
}

/**
 * @brief Audio sample storage type.
 */
enum class AudioSampleType { U8, S16, S24, S32, F32 };

/**
 * @brief Returns the byte size of one audio sample of the given type.
 * @param t Audio sample storage type.
 * @return Byte size of one sample.
 */
inline size_t getAudioSampleByteSize(AudioSampleType t) {
    switch (t) {
    case AudioSampleType::U8:
        return 1;
    case AudioSampleType::S16:
        return 2;
    case AudioSampleType::S24:
        return 3;
    case AudioSampleType::S32:
    case AudioSampleType::F32:
        return 4;
    }
    throw std::runtime_error("Unknown AudioSampleType in getAudioSampleByteSize()");
}

/**
 * @brief Per-channel quantization scale and zero-point.
 */
struct QuantizationArgs {
    float scale = 1.0f;     ///< Quantization scale factor.
    float zeroPoint = 0.0f; ///< Quantization zero point.
};

/**
 * @brief Per-channel normalisation mean and standard deviation helpers.
 */
struct MeanStd {
    /**
     * @brief Returns true if @p value equals the default mean (0, 0, 0).
     * @param value Per-channel mean colour to test.
     */
    static bool isDefaultMean(const pek::Colorf &value) {
        if (value.r != 0.0f || value.g != 0.0f || value.b != 0.0f)
            return false;
        return true;
    }

    /**
     * @brief Returns true if @p value equals the default std (1, 1, 1).
     * @param value Per-channel standard deviation colour to test.
     */
    static bool isDefaultStd(const pek::Colorf &value) {
        if (value.r != 1.0f || value.g != 1.0f || value.b != 1.0f)
            return false;
        return true;
    }
};

/**
 * @brief Indicates whether a tensor is used as model input or output.
 */
enum class TensorInOut { In, Out };

/**
 * @brief Describes the semantic content stored in a tensor.
 */
enum class DataKind {
    Unknown = 0,
    ImageRgbChw,  ///< Planar RGB image, layout RRRGGGBBB.
    ImageRgbHwc,  ///< Interleaved RGB image, layout RGBRGBRGB.
    ImageBgraHwc, ///< Interleaved BGRA image, layout BGRABGRA.
    ImageGray,    ///< Single-channel greyscale image.

    AudioPcm,    ///< PCM audio samples.
    AudioLogMel, ///< Log-mel audio samples.

    RawTensorData, ///< Raw (possibly quantized) tensor data, e.g. a model output buffer.

    Value,   ///< Single scalar value.
    Vector2, ///< Two scalar values.
    Vector3, ///< Three scalar values.
    Vector4  ///< Four scalar values.
};

/**
 * @brief Returns true if @p kind represents a scalar or small vector data kind.
 * @param kind DataKind to test.
 */
inline bool isScalarDataKind(DataKind kind) {
    switch (kind) {
    case DataKind::Value:
    case DataKind::Vector2:
    case DataKind::Vector3:
    case DataKind::Vector4:
        return true;
    default:
        return false;
    }
}

/**
 * @brief Returns true if @p kind represents an image data kind.
 * @param kind DataKind to test.
 */
inline bool isImageDataKind(DataKind kind) {
    switch (kind) {
    case DataKind::ImageRgbChw:
    case DataKind::ImageRgbHwc:
    case DataKind::ImageBgraHwc:
    case DataKind::ImageGray:
        return true;
    default:
        return false;
    }
}

/// @brief Maximum number of input or output tensors per inference op.
constexpr size_t MaxTensorCount = 8;
/// @brief Sentinel value for an uninitialised or invalid tensor index.
constexpr size_t InvalidTensorIndex = std::numeric_limits<size_t>::max();

/**
 * @brief Physical and model image dimensions associated with an inference.
 */
struct ImageInferenceMetadata {
    size_t width = 0;           ///< Physical image width in pixels.
    size_t height = 0;          ///< Physical image height in pixels.
    size_t modelWidth = 0;      ///< Input tensor width expected by the model.
    size_t modelHeight = 0;     ///< Input tensor height expected by the model.
    size_t letterboxLeft = 0;   ///< Left padding in model input pixels.
    size_t letterboxRight = 0;  ///< Right padding in model input pixels.
    size_t letterboxTop = 0;    ///< Top padding in model input pixels.
    size_t letterboxBottom = 0; ///< Bottom padding in model input pixels.
};

/**
 * @brief Contextual information about a single inference execution.
 */
struct InferenceInfo {
    uint64_t parentUuid = 0;      ///< UUID of the parent Perception frame.
    std::string contentType;      ///< MIME-style content type identifier.
    std::string modelFamily;      ///< Model family name, e.g. "yolov11".
    std::string inferElementId;   ///< GStreamer element id of the originating pekinfer.
    ImageInferenceMetadata image; ///< Image geometry for this inference.
};

/**
 * @brief Axis-aligned pixel rectangle.
 */
struct PixelRect {
    size_t x = 0;      ///< Left edge in pixels.
    size_t y = 0;      ///< Top edge in pixels.
    size_t width = 0;  ///< Width in pixels.
    size_t height = 0; ///< Height in pixels.

    /**
     * @brief Returns true if the rectangle has zero area.
     */
    bool isEmpty() const {
        return width == 0 || height == 0;
    }

    /**
     * @brief Returns true if the rectangle fits entirely within the given surface dimensions.
     * @param surfaceWidth Surface width in pixels.
     * @param surfaceHeight Surface height in pixels.
     */
    bool fitsWithin(size_t surfaceWidth, size_t surfaceHeight) const {
        return x <= surfaceWidth && y <= surfaceHeight && width <= surfaceWidth - x &&
               height <= surfaceHeight - y;
    }
};

/**
 * @brief Pixel sampling mode for image resizing operations.
 */
enum class Sampling {
    Nearest, ///< Nearest-neighbour sampling.
    Linear   ///< Bilinear sampling (reserved, not yet supported).
};

/**
 * @brief Describes a tensor feedback loop, copying an output tensor back as a future input.
 */
struct TensorFeedback {
    /**
     * @brief Copy mode for the feedback operation.
     */
    enum class Mode {
        Copy ///< Direct buffer copy.
    };

    size_t fromOutputTensorIndex = 0; ///< Source output tensor index.
    size_t toInputTensorIndex = 0;    ///< Destination input tensor index.
    Mode mode = Mode::Copy;           ///< Feedback copy mode.
};

/**
 * @brief Describes where externally supplied media or tensor memory is stored.
 */
enum class MemoryType {
    Unknown = 0, ///< Memory backend is unspecified or unsupported.
    Host,        ///< CPU-addressable host memory.
    DmaBuf,      ///< Linux DMA-BUF file-descriptor backed memory.
};

/**
 * @brief Describes permitted access for a non-owning memory view.
 */
enum class AccessMode {
    Unknown = 0, ///< Access permissions are unspecified.
    Read,        ///< Memory may be read but not written.
    Write,       ///< Memory may be written but not read.
    ReadWrite,   ///< Memory may be read and written.
};

} // namespace pek
