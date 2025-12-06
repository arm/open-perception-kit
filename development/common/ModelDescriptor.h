#pragma once

#include "uniflow/public_types.h"
#include <string>
#include <nlohmann/json.hpp>
#include "JsonSchemas.h"

struct ModelDescriptor {

    std::string name;

    std::string builderId;
    std::string parserId;

    bool dynamicInput = false;
    bool dynamicOutput = false;

    bool forceInputType = false;
    uflw::ValueType inputType;

    bool forceOutputType = false;
    uflw::ValueType outputType;
    
    uflw::Shape inputShape;
    uflw::Shape outputShape;

    size_t maxDetectionCount = 16;
    float confidenceThreshold = 0.7f;
};

using nlohmann::json;

inline void to_json(json& j, const ModelDescriptor& b) {
    j = json {
        { "name", b.name },
        { "builderId", b.builderId },
        { "parserId", b.parserId },
        { "dynamicInput", b.dynamicInput },
        { "dynamicOutput", b.dynamicOutput },
        { "inputShape", b.inputShape },
        { "maxDetectionCount", b.maxDetectionCount },
        { "confidenceThreshold", b.confidenceThreshold }
    };
}

inline void from_json(const json& j, ModelDescriptor& b) {
    j.at("name").get_to(b.name);
    j.at("parserId").get_to(b.parserId);
    j.at("builderId").get_to(b.builderId);
    j.at("dynamicInput").get_to(b.dynamicInput);
    j.at("dynamicOutput").get_to(b.dynamicOutput);
    j.at("inputShape").get_to(b.inputShape);
    j.at("maxDetectionCount").get_to(b.maxDetectionCount);
    j.at("confidenceThreshold").get_to(b.confidenceThreshold);
}

/*
inline void to_json(json& j, const uflw::ValueType& b) {
    j = json {
        { "name", b.name },
        { "builderId", b.builderId },
        { "parserId", b.parserId },
        { "dynamicInput", b.dynamicInput },
        { "dynamicOutput", b.dynamicOutput },
        { "inputShape", b.inputShape }
    };
}

inline void from_json(const json& j, const uflw::ValueType& b) {
    j.at("name").get_to(b.name);
    j.at("parserId").get_to(b.parserId);
    j.at("builderId").get_to(b.builderId);
    j.at("dynamicInput").get_to(b.dynamicInput);
    j.at("dynamicOutput").get_to(b.dynamicOutput);
    j.at("inputShape").get_to(b.inputShape);
}
*/



