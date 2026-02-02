#pragma once

#include "amp/Shape.h"
#include "amp/Types.h"

#include "JsonSchemas.h"

#include <nlohmann/json.hpp>
#include <string>

#include "amp/Result.h"

using nlohmann::json;

struct TensorDescriptor {
    amp::Shape shape{}; // if this is missing, system tries to discover it using onnx
    amp::DataKind dataKind = amp::DataKind::Unknown; // e.g. ImageRgbChw

    amp::Tdt tdt = amp::Tdt::Float32;
    float zeroPoint = 0.0f;
    float scale = 1.0f;

    std::vector<float> valueInputs;
};

struct ModelDescriptor {

    std::string name;

    std::string modelFile;
    std::string modelFamily; // "yolo-object-detection", "blazeface"

    std::vector<TensorDescriptor> inputTensors;
    std::vector<TensorDescriptor> outputTensors;

    // sometimes onnx reports an output tensor shape but the models fails to use it
    // set to true to let the model decide the output (tensor reallocation in every inference step)
    bool dynamicOutput = false;
    amp::Tdt outputTdtType;

    size_t maxDetectionCount = 16;
    float confidenceThreshold = 0.7f;
    float iouThreshold = 0.5f;

    static amp::Result<ModelDescriptor> fromJson(const std::string &jsonString);
    static amp::Result<ModelDescriptor> fromFile(const std::string &path);
};

// ---

inline void to_json(json &j, const TensorDescriptor &b) {
    j = json{
        {"shape", b.shape},
        {"valueType", b.tdt},
        {"zeroPoint", b.zeroPoint},
        {"scale", b.scale},
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
    j.at("dataKind").get_to(b.dataKind);
    //    b.dataKind = j.value("dataKind", amp::DataKind::Unknown);
    b.valueInputs = j.value("valueInputs", std::vector<float>{});
}

inline void to_json(json &j, const ModelDescriptor &b) {
    j = json{{"name", b.name},
             {"modelFile", b.modelFile},
             {"modelFamily", b.modelFamily},
             {"inputTensors", b.inputTensors},
             {"outputTensors", b.outputTensors},
             {"dynamicOutput", b.dynamicOutput},
             {"maxDetectionCount", b.maxDetectionCount},
             {"confidenceThreshold", b.confidenceThreshold},
             {"iouThreshold", b.iouThreshold}};
}

inline void from_json(const json &j, ModelDescriptor &b) {
    j.at("name").get_to(b.name);
    j.at("modelFile").get_to(b.modelFile);
    j.at("modelFamily").get_to(b.modelFamily);
    b.inputTensors = j.value("inputTensors", std::vector<TensorDescriptor>{});
    b.outputTensors = j.value("outputTensors", std::vector<TensorDescriptor>{});
    j.at("dynamicOutput").get_to(b.dynamicOutput);
    j.at("maxDetectionCount").get_to(b.maxDetectionCount);
    j.at("confidenceThreshold").get_to(b.confidenceThreshold);
    j.at("iouThreshold").get_to(b.iouThreshold);
}
