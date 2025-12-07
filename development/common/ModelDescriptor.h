#pragma once

#include "uniflow/public_types.h"
#include <string>
#include <nlohmann/json.hpp>
#include "JsonSchemas.h"

#include "amp/Result.h"

struct ModelDescriptor {

    std::string name;
    std::string modelFile;

    std::string builderId;
    std::string parserId;

    uflw::Shape inputShape = uflw::Shape { 0 };

    bool dynamicOutput = false;
    
    size_t maxDetectionCount = 16;
    float confidenceThreshold = 0.7f;

    static amp::Result<ModelDescriptor> fromJson(const std::string& jsonString);
    static amp::Result<ModelDescriptor> fromFile(const std::string& path);

};

using nlohmann::json;

inline void to_json(json& j, const ModelDescriptor& b) {
    j = json {
        { "name", b.name },
        { "modelFile", b.modelFile },
        { "builderId", b.builderId },
        { "parserId", b.parserId },
        { "dynamicOutput", b.dynamicOutput },
        { "inputShape", b.inputShape },
        { "maxDetectionCount", b.maxDetectionCount },
        { "confidenceThreshold", b.confidenceThreshold }
    };
}

inline void from_json(const json& j, ModelDescriptor& b) {
    j.at("name").get_to(b.name);
    j.at("modelFile").get_to(b.modelFile);
    j.at("parserId").get_to(b.parserId);
    j.at("builderId").get_to(b.builderId);
    j.at("dynamicOutput").get_to(b.dynamicOutput);
    j.at("inputShape").get_to(b.inputShape);
    j.at("maxDetectionCount").get_to(b.maxDetectionCount);
    j.at("confidenceThreshold").get_to(b.confidenceThreshold);
}

