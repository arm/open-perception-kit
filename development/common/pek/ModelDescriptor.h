/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Color.h"
#include "pek/JsonSchemas.h"
#include "pek/Result.h"
#include "pek/Shape.h"
#include "pek/Types.h"

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace pek {

inline constexpr float DefaultLetterboxColor = 114.0f / 255.0f;

/**
 * @brief JSON-serializable tensor descriptor used by model descriptors.
 *
 * Describes expected tensor shape/layout/type and optional preprocessing
 * parameters for input/output tensors.
 */
struct TensorDescriptor {
    /// Tensor shape. When missing/invalid, runtime may try to infer it from model metadata.
    pek::Shape shape{};

    /// Tensor semantic kind (for example ImageRgbChw).
    pek::DataKind dataKind = pek::DataKind::Unknown;

    /// Tensor element type.
    pek::Dtype dtype = pek::Dtype::Float32;

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

    /// Model file path, relative to the descriptor or absolute.
    std::string modelFile;

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

    /**
     * @brief Builds a descriptor from JSON text.
     * @param jsonString JSON payload.
     * @param source Descriptor source path used for diagnostics and filename routing.
     * @return Parsed descriptor or error.
     */
    static pek::Result<ModelDescriptor> fromJson(const std::string &jsonString,
                                                 const std::string &source = "model.json");

    /**
     * @brief Loads and parses a descriptor from a JSON file.
     * @param path JSON file path.
     * @return Parsed descriptor with relative modelFile joined to path without lexical
     * normalization, or error. Absolute modelFile values are preserved.
     */
    static pek::Result<ModelDescriptor> fromFile(const std::string &path);

    /// Optional tensor feedback loop descriptors.
    std::vector<pek::TensorFeedback> tensorFeedbacks;
};

// ---

inline void to_json(json &j, const TensorDescriptor &b) {
    j = json{{"dataKind", b.dataKind}, {"valueType", b.dtype}};

    const bool image = b.dataKind == pek::DataKind::ImageRgbChw ||
                       b.dataKind == pek::DataKind::ImageRgbHwc ||
                       b.dataKind == pek::DataKind::ImageGray;
    const bool floating = b.dtype == pek::Dtype::Float16 || b.dtype == pek::Dtype::Float32;
    const bool values =
        b.dataKind == pek::DataKind::Value || b.dataKind == pek::DataKind::Vector2 ||
        b.dataKind == pek::DataKind::Vector3 || b.dataKind == pek::DataKind::Vector4;

    if (!values)
        j["shape"] = b.shape;
    if (image) {
        j["keepAspectRatio"] = b.keepAspectRatio;
        if (floating) {
            j["mean"] = b.mean;
            j["std"] = b.std;
        }
        if (b.keepAspectRatio) {
            j["letterboxRed"] = b.letterboxRed;
            j["letterboxGreen"] = b.letterboxGreen;
            j["letterboxBlue"] = b.letterboxBlue;
        }
    }
    if (b.dataKind == pek::DataKind::RawTensorData &&
        b.matchShapeOutputIndex != pek::InvalidTensorIndex)
        j["matchShapeOutputIndex"] = b.matchShapeOutputIndex;
    if (values)
        j["valueInputs"] = b.valueInputs;
}

inline void from_json(const json &j, TensorDescriptor &b) {
    b.shape = j.value("shape", pek::Shape());
    b.dtype = j.value("valueType", pek::Dtype::Float32);
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
    j = json{{"version", 1},
             {"name", b.name},
             {"modelFile", b.modelFile},
             {"inputTensors", b.inputTensors},
             {"dynamicOutput", b.dynamicOutput}};
    if (!b.contentType.empty())
        j["contentType"] = b.contentType;
    if (!b.outputTensors.empty())
        j["outputTensors"] = b.outputTensors;
    if (!b.tensorFeedbacks.empty())
        j["tensorFeedbacks"] = b.tensorFeedbacks;
    if (!b.legal.empty())
        j["legal"] = b.legal;
}

inline void from_json(const json &j, ModelDescriptor &b) {
    j.at("name").get_to(b.name);
    j.at("modelFile").get_to(b.modelFile);
    b.contentType = j.value("contentType", std::string{});
    b.inputTensors = j.value("inputTensors", std::vector<TensorDescriptor>{});
    b.outputTensors = j.value("outputTensors", std::vector<TensorDescriptor>{});
    b.tensorFeedbacks = j.value("tensorFeedbacks", std::vector<pek::TensorFeedback>{});
    b.legal = j.value("legal", std::string{});
    j.at("dynamicOutput").get_to(b.dynamicOutput);
}
} // namespace pek
