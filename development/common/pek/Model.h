/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/ModelDescriptor.h"
#include "pek/Result.h"
#include "pek/Shape.h"
#include "pek/TensorView.h"
#include "pek/Types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pek {

/**
 * @brief Runtime input tensor description used by a loaded model.
 */
struct ModelInput {
    /// Input tensor name.
    std::string name;

    /// Semantic input kind (image, scalar value, vectors, etc.).
    pek::DataKind dataKind = pek::DataKind::Unknown;

    /// Element type.
    pek::Dtype valueType = pek::Dtype::Float32;

    /// Input tensor shape.
    pek::Shape shape{};

    /// Batch size when available.
    int batch = 0;

    /// Quantization arguments.
    pek::QuantizationArgs quantArguments;

    /// Optional normalization mean/std values.
    pek::Colorf mean = {0.0f, 0.0f, 0.0f, 0.0f}, std = {1.0f, 1.0f, 1.0f, 1.0f};

    /// Preserve image aspect ratio during image tensor resize by letterboxing.
    bool keepAspectRatio = false;

    /// Letterbox padding color in normalized RGB channel values.
    float letterboxRed = DefaultLetterboxColor;
    float letterboxGreen = DefaultLetterboxColor;
    float letterboxBlue = DefaultLetterboxColor;

    /// Optional scalar/vector input values for non-image tensor kinds.
    std::vector<float> valueInputs;

    /**
     * @brief Optional output tensor index used for shape matching.
     *
     * If not pek::InvalidTensorIndex, runtime can reallocate this input tensor to
     * match the shape of the referenced output tensor.
     */
    size_t matchShapeOutputIndex = pek::InvalidTensorIndex;

    /**
     * @brief Tries to extract image width/height from a 4D tensor shape.
     * @param outWidth Output width.
     * @param outHeight Output height.
     * @return True when the shape looks like a 1/3-channel image tensor.
     */
    bool tryGetImageTensorSize(size_t &outWidth, size_t &outHeight) const;
};

/**
 * @brief Runtime output tensor description used by a loaded model.
 */
struct ModelOutput {
    /// Output tensor name.
    std::string name;

    /// Element type.
    pek::Dtype valueType = pek::Dtype::Float32;

    /// Output tensor shape.
    pek::Shape shape;

    /// Quantization arguments.
    pek::QuantizationArgs quantArguments;
};

/**
 * @brief Unified runtime model view composed from model metadata and JSON descriptor.
 */
struct Model {

    /// True when the model itself determines/applies input size.
    bool inputSizeAppliedByModel = false;

    /// True when NMS is already applied by the model.
    bool nmsAppliedByModel = false;

    /// Human-readable model name.
    std::string name;

    /// Runtime engine identifier.
    std::string engine;

    /// Optional semantic content type.
    std::string contentType;

    /// Runtime input tensors.
    std::vector<ModelInput> inputs;

    /// Runtime output tensors.
    std::vector<ModelOutput> outputs;

    /// Optional tensor feedback loop descriptors.
    std::vector<pek::TensorFeedback> tensorFeedbacks;

    /**
     * @brief Enables models where output size is only known during inference execution.
     */
    bool useDynamicOutput = false;

    /**
     * @brief Creates a tensor view over an output tensor using the configured output shape.
     * @param index Output tensor index.
     * @param data Pointer to tensor memory.
     * @return Tensor view for the selected output.
     */
    TensorView createOutputTensorView(size_t index, const uint8_t *data) const;

    /**
     * @brief Creates a tensor view over an output tensor using a caller-provided shape.
     * @param index Output tensor index.
     * @param data Pointer to tensor memory.
     * @param shape Shape used for the view.
     * @return Tensor view for the selected output.
     */
    TensorView
    createOutputTensorView(size_t index, const uint8_t *data, const pek::Shape &shape) const;

    /**
     * @brief Merges runtime model metadata with JSON descriptor metadata.
     *
     * The method is meant to be called on the model instance built from the model file.
     * It then applies descriptor data from JSON and validates that the two representations
     * can be unified into a final runtime model view.
     *
     * @param modelDescriptor Descriptor loaded from JSON.
     * @return Success or validation error.
     */
    pek::Result<void> applyModelFromDescriptor(const ModelDescriptor &modelDescriptor);

    /**
     * @brief Builds a human-readable model summary.
     * @return Multi-line text representation of model I/O and metadata.
     */
    std::string toString() const;
};

} // namespace pek
