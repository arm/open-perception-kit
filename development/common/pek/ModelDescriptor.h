/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Color.h"
#include "pek/JsonSchemas.h"
#include "pek/Result.h"
#include "pek/Shape.h"
#include "pek/Types.h"

#include <stop_token>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace pek {

inline constexpr float DefaultLetterboxColor = 114.0f / 255.0f;

/**
 * @brief JSON-serializable tensor descriptor used by model descriptors.
 *
 * Describes expected tensor shape/layout/type and optional quantization and
 * preprocessing parameters for input/output tensors.
 */
struct TensorDescriptor {
    /// Tensor shape. When missing/invalid, runtime may try to infer it from model metadata.
    pek::Shape shape{};

    /// Tensor semantic kind (for example ImageRgbChw).
    pek::DataKind dataKind = pek::DataKind::Unknown;

    /// Tensor element type.
    pek::Dtype dtype = pek::Dtype::Float32;

    /// Quantization zero point.
    float zeroPoint = 0.0f;

    /// Quantization scale.
    float scale = 1.0f;

    /// Per-channel mean/std normalization parameters.
    pek::Colorf mean = {0.0f, 0.0f, 0.0f, 0.0f}, std = {1.0f, 1.0f, 1.0f, 1.0f};

    /// Preserve image aspect ratio during image tensor resize by letterboxing.
    bool keepAspectRatio = false;

    /// Letterbox padding color in normalized RGB channel values.
    float letterboxRed = DefaultLetterboxColor;
    float letterboxGreen = DefaultLetterboxColor;
    float letterboxBlue = DefaultLetterboxColor;

    /**
     * @brief Optional output tensor index used for dynamic shape matching.
     *
     * If this is an input tensor and value is not pek::InvalidTensorIndex,
     * the input tensor may be reallocated to match the referenced output shape.
     */
    size_t matchShapeOutputIndex = pek::InvalidTensorIndex;

    /// Optional scalar/vector input values for non-image tensor kinds.
    std::vector<float> valueInputs;
};

/**
 * @brief JSON-serializable model descriptor.
 *
 * Contains model identity, tensor descriptors, and runtime behavior hints used
 * to configure inference pipelines.
 */
struct ModelDescriptor {

    /// Human-readable model name.
    std::string name;

    /// Optional legal/license notice associated with the model.
    std::string legal;

    /// Local relative model path or immutable published model asset locator.
    std::string modelFile;

    /// Model family identifier (for example "yolov11").
    std::string modelFamily;

    /// Semantic model content type (for example detection/classification).
    std::string contentType;

    /// Input tensor descriptors.
    std::vector<TensorDescriptor> inputTensors;

    /// Output tensor descriptors.
    std::vector<TensorDescriptor> outputTensors;

    /**
     * @brief Enables runtime-dynamic output shape handling.
     *
     * Some models/runtimes expose output shapes that are not stable at configuration
     * time. When enabled, runtime can reallocate output tensors per inference step.
     */
    bool dynamicOutput = false;

    /// Optional output dtype override/hint.
    pek::Dtype outputDtype = pek::Dtype::Float32;

    /**
     * @brief Builds a descriptor from JSON text.
     * @param jsonString JSON payload.
     * @return Parsed descriptor or error.
     */
    static pek::Result<ModelDescriptor> fromJson(const std::string &jsonString);

    /**
     * @brief Loads and materializes a descriptor synchronously from a JSON file.
     *
     * Callers that need asynchronous behavior choose the execution thread and may
     * provide load controls.
     *
     * @param path JSON file path.
     * @param stopToken Optional cooperative cancellation token.
     * @return Parsed descriptor with a resolved local model path, or an error.
     */
    static pek::Result<ModelDescriptor> fromFile(const std::string &path,
                                                 std::stop_token stopToken = {});

    /// Optional tensor feedback loop descriptors.
    std::vector<pek::TensorFeedback> tensorFeedbacks;
};

// ---

inline void to_json(json &j, const TensorDescriptor &b) {
    j = json{
        {"shape", b.shape},
        {"valueType", b.dtype},
        {"zeroPoint", b.zeroPoint},
        {"scale", b.scale},
        {"mean", b.mean},
        {"std", b.std},
        {"keepAspectRatio", b.keepAspectRatio},
        {"letterboxRed", b.letterboxRed},
        {"letterboxGreen", b.letterboxGreen},
        {"letterboxBlue", b.letterboxBlue},
        {"matchShapeOutputIndex", b.matchShapeOutputIndex},
        {"dataKind", b.dataKind},
        {"valueInputs", b.valueInputs},
    };
}

inline void from_json(const json &j, TensorDescriptor &b) {
    b.shape = j.value("shape", pek::Shape());
    b.dtype = j.value("valueType", pek::Dtype::Float32);
    b.zeroPoint = j.value("zeroPoint", 0.0f);
    b.scale = j.value("scale", 1.0f);
    b.mean = j.value("mean", pek::Colorf{0.0f, 0.0f, 0.0f, 0.0f});
    b.std = j.value("std", pek::Colorf{1.0f, 1.0f, 1.0f, 1.0f});
    b.keepAspectRatio = j.value("keepAspectRatio", false);
    b.letterboxRed = j.value("letterboxRed", DefaultLetterboxColor);
    b.letterboxGreen = j.value("letterboxGreen", DefaultLetterboxColor);
    b.letterboxBlue = j.value("letterboxBlue", DefaultLetterboxColor);
    b.matchShapeOutputIndex = j.value("matchShapeOutputIndex", pek::InvalidTensorIndex);
    j.at("dataKind").get_to(b.dataKind);
    b.valueInputs = j.value("valueInputs", std::vector<float>{});
}

inline void to_json(json &j, const ModelDescriptor &b) {
    j = json{{"name", b.name},
             {"modelFile", b.modelFile},
             {"modelFamily", b.modelFamily},
             {"contentType", b.contentType},
             {"inputTensors", b.inputTensors},
             {"outputTensors", b.outputTensors},
             {"tensorFeedbacks", b.tensorFeedbacks},
             {"legal", b.legal},
             {"dynamicOutput", b.dynamicOutput}};
}

inline void from_json(const json &j, ModelDescriptor &b) {
    j.at("name").get_to(b.name);
    j.at("modelFile").get_to(b.modelFile);
    j.at("modelFamily").get_to(b.modelFamily);
    b.contentType = j.value("contentType", std::string{});
    b.inputTensors = j.value("inputTensors", std::vector<TensorDescriptor>{});
    b.outputTensors = j.value("outputTensors", std::vector<TensorDescriptor>{});
    b.tensorFeedbacks = j.value("tensorFeedbacks", std::vector<pek::TensorFeedback>{});
    b.legal = j.value("legal", std::string{});
    j.at("dynamicOutput").get_to(b.dynamicOutput);
}
} // namespace pek
