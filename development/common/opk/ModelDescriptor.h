/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "opk/Color.h"
#include "opk/JsonSchemas.h"
#include "opk/Result.h"
#include "opk/Shape.h"
#include "opk/Types.h"

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace opk {

inline constexpr float DefaultLetterboxColor = 114.0f / 255.0f;

/**
 * @brief JSON-serializable tensor descriptor used by model descriptors.
 *
 * Describes expected tensor shape/layout/type and optional preprocessing
 * parameters for input/output tensors.
 */
struct TensorDescriptor {
    /// Tensor shape. When missing/invalid, runtime may try to infer it from model metadata.
    opk::Shape shape{};

    /// Tensor semantic kind (for example ImageRgbChw).
    opk::DataKind dataKind = opk::DataKind::Unknown;

    /// Tensor element type.
    opk::Dtype dtype = opk::Dtype::Float32;

    /// Per-channel mean/std normalization parameters.
    opk::Colorf mean = {0.0f, 0.0f, 0.0f, 0.0f}, std = {1.0f, 1.0f, 1.0f, 1.0f};

    /// Preserve image aspect ratio during image tensor resize by letterboxing.
    bool keepAspectRatio = false;

    /// Letterbox padding color in normalized RGB channel values.
    float letterboxRed = DefaultLetterboxColor;
    float letterboxGreen = DefaultLetterboxColor;
    float letterboxBlue = DefaultLetterboxColor;

    /**
     * @brief Optional output tensor index used for dynamic shape matching.
     *
     * If this is an input tensor and value is not opk::InvalidTensorIndex,
     * the input tensor may be reallocated to match the referenced output shape.
     */
    size_t matchShapeOutputIndex = opk::InvalidTensorIndex;

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
    static opk::Result<ModelDescriptor> fromJson(const std::string &jsonString,
                                                 const std::string &source = "model.json");

    /**
     * @brief Loads and parses a descriptor from a JSON file.
     * @param path JSON file path.
     * @return Parsed descriptor with relative modelFile joined to path without lexical
     * normalization, or error. Absolute modelFile values are preserved.
     */
    static opk::Result<ModelDescriptor> fromFile(const std::string &path);

    /// Optional tensor feedback loop descriptors.
    std::vector<opk::TensorFeedback> tensorFeedbacks;

    /// Configuration contract version, preserved when serializing the descriptor.
    std::string version;
};

// ---

inline void to_json(json &j, const TensorDescriptor &b) {
    j = json{{"dataKind", b.dataKind}, {"valueType", b.dtype}};

    const bool image = b.dataKind == opk::DataKind::ImageRgbChw ||
                       b.dataKind == opk::DataKind::ImageRgbHwc ||
                       b.dataKind == opk::DataKind::ImageGray;
    const bool floating = b.dtype == opk::Dtype::Float16 || b.dtype == opk::Dtype::Float32;
    const bool values =
        b.dataKind == opk::DataKind::Value || b.dataKind == opk::DataKind::Vector2 ||
        b.dataKind == opk::DataKind::Vector3 || b.dataKind == opk::DataKind::Vector4;

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
    if (b.dataKind == opk::DataKind::RawTensorData &&
        b.matchShapeOutputIndex != opk::InvalidTensorIndex)
        j["matchShapeOutputIndex"] = b.matchShapeOutputIndex;
    if (values)
        j["valueInputs"] = b.valueInputs;
}

inline void from_json(const json &j, TensorDescriptor &b) {
    b.shape = j.value("shape", opk::Shape());
    b.dtype = j.value("valueType", opk::Dtype::Float32);
    b.mean = j.value("mean", opk::Colorf{0.0f, 0.0f, 0.0f, 0.0f});
    b.std = j.value("std", opk::Colorf{1.0f, 1.0f, 1.0f, 1.0f});
    b.keepAspectRatio = j.value("keepAspectRatio", false);
    b.letterboxRed = j.value("letterboxRed", DefaultLetterboxColor);
    b.letterboxGreen = j.value("letterboxGreen", DefaultLetterboxColor);
    b.letterboxBlue = j.value("letterboxBlue", DefaultLetterboxColor);
    b.matchShapeOutputIndex = j.value("matchShapeOutputIndex", opk::InvalidTensorIndex);
    j.at("dataKind").get_to(b.dataKind);
    b.valueInputs = j.value("valueInputs", std::vector<float>{});
}

inline void to_json(json &j, const ModelDescriptor &b) {
    j = json{{"version", b.version},
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
    j.at("version").get_to(b.version);
    j.at("name").get_to(b.name);
    j.at("modelFile").get_to(b.modelFile);
    b.contentType = j.value("contentType", std::string{});
    b.inputTensors = j.value("inputTensors", std::vector<TensorDescriptor>{});
    b.outputTensors = j.value("outputTensors", std::vector<TensorDescriptor>{});
    b.tensorFeedbacks = j.value("tensorFeedbacks", std::vector<opk::TensorFeedback>{});
    b.legal = j.value("legal", std::string{});
    j.at("dynamicOutput").get_to(b.dynamicOutput);
}
} // namespace opk
