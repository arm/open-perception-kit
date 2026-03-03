/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "amp/Shape.h"
#include "amp/Types.h"

#include "amp/JsonSchemas.h"

#include <nlohmann/json.hpp>
#include <string>

#include "amp/Color.h"
#include "amp/Result.h"

using nlohmann::json;

// namespace amp {

struct TensorDescriptor {
    amp::Shape shape{}; // if this is missing, system tries to discover it using onnx
    amp::DataKind dataKind = amp::DataKind::Unknown; // e.g. ImageRgbChw

    amp::Tdt tdt = amp::Tdt::Float32;
    float zeroPoint = 0.0f;
    float scale = 1.0f;
    amp::Colorf mean = {0.0f, 0.0f, 0.0f, 0.0f}, std = {1.0f, 1.0f, 1.0f, 1.0f};

    // if this is an input tensor and the value is not amp::InvalidTensorIndex
    // we have to realloc the tensor to match the shape of the referenced output tensor
    size_t matchShapeOutputIndex = amp::InvalidTensorIndex;

    std::vector<float> valueInputs;
};

struct ModelDescriptor {

    std::string name;
    std::string legal;

    std::string modelFile;
    std::string modelFamily;
    std::string contentType;

    std::vector<TensorDescriptor> inputTensors;
    std::vector<TensorDescriptor> outputTensors;

    // sometimes onnx reports an output tensor shape but the models fails to use it
    // set to true to let the model decide the output (tensor reallocation in every inference step)
    bool dynamicOutput = false;
    amp::Tdt outputTdtType;

    static amp::Result<ModelDescriptor> fromJson(const std::string &jsonString);
    static amp::Result<ModelDescriptor> fromFile(const std::string &path);

    std::vector<amp::TensorFeedback> tensorFeedbacks;
};

// ---

inline void to_json(json &j, const TensorDescriptor &b) {
    j = json{
        {"shape", b.shape},
        {"valueType", b.tdt},
        {"zeroPoint", b.zeroPoint},
        {"scale", b.scale},
        {"mean", b.mean},
        {"std", b.std},
        {"stmatchShapeOutputIndexd", b.matchShapeOutputIndex},
        {"dataKind", b.dataKind},
        {"valueInputs", b.valueInputs},
    };
}

inline void from_json(const json &j, TensorDescriptor &b) {
    //    j.at("shape").get_to(b.shape);
    b.shape = j.value("shape", amp::Shape());
    b.tdt = j.value("valueType", amp::Tdt::Float32);
    b.zeroPoint = j.value("zeroPoint", 0.0f);
    b.scale = j.value("scale", 1.0f);
    b.mean = j.value("mean", amp::Colorf{0.0f, 0.0f, 0.0f, 0.0f});
    b.std = j.value("std", amp::Colorf{1.0f, 1.0f, 1.0f, 1.0f});
    b.matchShapeOutputIndex = j.value("matchShapeOutputIndex", amp::InvalidTensorIndex);
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
    b.tensorFeedbacks = j.value("tensorFeedbacks", std::vector<amp::TensorFeedback>{});
    b.legal = j.value("legal", std::string{});
    j.at("dynamicOutput").get_to(b.dynamicOutput);
}
//}
