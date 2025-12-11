#pragma once

#include "uniflow/model_io.h"
#include "uniflow/public_types.h"
#include <string>
#include <nlohmann/json.hpp>
#include "JsonSchemas.h"

#include "amp/Result.h"

using nlohmann::json;

struct TensorDescriptor {
    uflw::Shape shape { }; // if this is missing, system tries to discover it using onnx
    uflw::TensorDataKind dataKind = uflw::TensorDataKind::Unknown; // e.g. ImageRgbChw
    
    uflw::ValueType valueType = uflw::ValueType::f32;
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
    uflw::ValueType outputValueType;
    
    size_t maxDetectionCount = 16;
    float confidenceThreshold = 0.7f;

    static amp::Result<ModelDescriptor> fromJson(const std::string& jsonString);
    static amp::Result<ModelDescriptor> fromFile(const std::string& path);

};

// ---

inline void to_json(json& j, const TensorDescriptor& b) {
    j = json {
        { "shape", b.shape },
        { "valueType", b.valueType },
        { "zeroPoint", b.zeroPoint },
        { "scale", b.scale },
        { "dataKind", b.dataKind },
        { "valueInputs", b.valueInputs },
    };
}

inline void from_json(const json& j, TensorDescriptor& b) {
//    j.at("shape").get_to(b.shape);
    b.shape = j.value("shape", uflw::Shape());
    b.valueType = j.value("valueType", uflw::ValueType::f32);
    b.zeroPoint = j.value("zeroPoint", 0.0f);
    b.scale = j.value("scale", 1.0f);
     j.at("dataKind").get_to(b.dataKind);
//    b.dataKind = j.value("dataKind", uflw::TensorDataKind::Unknown);
    b.valueInputs = j.value("valueInputs", std::vector<float>{});
}

inline void to_json(json& j, const ModelDescriptor& b) {
    j = json {
        { "name", b.name },
        { "modelFile", b.modelFile },
        { "modelFamily", b.modelFamily },
        { "inputTensors", b.inputTensors },
        { "outputTensors", b.outputTensors },
        { "dynamicOutput", b.dynamicOutput },
        { "maxDetectionCount", b.maxDetectionCount },
        { "confidenceThreshold", b.confidenceThreshold }
    };
}

inline void from_json(const json& j, ModelDescriptor& b) {
    j.at("name").get_to(b.name);
    j.at("modelFile").get_to(b.modelFile);
    j.at("modelFamily").get_to(b.modelFamily);
    b.inputTensors = j.value("inputTensors", std::vector<TensorDescriptor>{});
    b.outputTensors = j.value("outputTensors", std::vector<TensorDescriptor>{});
    j.at("dynamicOutput").get_to(b.dynamicOutput);
    j.at("maxDetectionCount").get_to(b.maxDetectionCount);
    j.at("confidenceThreshold").get_to(b.confidenceThreshold);
}

