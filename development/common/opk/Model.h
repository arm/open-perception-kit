/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

#include "opk/ModelDescriptor.h"
#include "opk/Result.h"
#include "opk/Shape.h"
#include "opk/TensorView.h"
#include "opk/Types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace opk {

/**
 * @brief Runtime input tensor description used by a loaded model.
 */
struct ModelInput {
    /// Input tensor name.
    std::string name;

    /// Semantic input kind (image, scalar value, vectors, etc.).
    opk::DataKind dataKind = opk::DataKind::Unknown;

    /// Element type.
    opk::Dtype valueType = opk::Dtype::Float32;

    /// Input tensor shape.
    opk::Shape shape{};

    /// Batch size when available.
    int batch = 0;

    /// Quantization arguments.
    opk::QuantizationArgs quantArguments;

    /// Optional normalization mean/std values.
    opk::Colorf mean = {0.0f, 0.0f, 0.0f, 0.0f}, std = {1.0f, 1.0f, 1.0f, 1.0f};

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
     * If not opk::InvalidTensorIndex, runtime can reallocate this input tensor to
     * match the shape of the referenced output tensor.
     */
    size_t matchShapeOutputIndex = opk::InvalidTensorIndex;

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
    opk::Dtype valueType = opk::Dtype::Float32;

    /// Output tensor shape.
    opk::Shape shape;

    /// Quantization arguments.
    opk::QuantizationArgs quantArguments;
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
    std::vector<opk::TensorFeedback> tensorFeedbacks;

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
    createOutputTensorView(size_t index, const uint8_t *data, const opk::Shape &shape) const;

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
    opk::Result<void> applyModelFromDescriptor(const ModelDescriptor &modelDescriptor);

    /**
     * @brief Builds a human-readable model summary.
     * @return Multi-line text representation of model I/O and metadata.
     */
    std::string toString() const;
};

} // namespace opk
