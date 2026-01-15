#pragma once

#include <onnxruntime_cxx_api.h>

#include "amp/Model.h"
#include "amp/PerceptionContext.h"
#include "amp/Result.h"
#include "amp/Shape.h"
#include "amp/Types.h"

#include "JsonSchemas.h"

#include <nlohmann/json.hpp>

#include <map>
#include <vector>

using nlohmann::json;

namespace onnx {

struct Tools {

    static bool onnxTypeToUniflowType(ONNXTensorElementDataType onnxType, amp::ValueType &outType);

    static std::vector<size_t>
    getTensorShape(const Ort::Session &session, amp::TensorInOut tensorInOut, int tensorIndex);
    static std::map<std::string, std::string> getModelMeta(const Ort::Session &session);
    static amp::Result<amp::Model> inspectModel(const Ort::Session &session);

    static std::string toString(const amp::Model &model);

    // ---

    static std::string serializeDetectionResult(const amp::DetectionResult &r) {
        json j = r;
        return j.dump();
    }

    static bool deserializeDetectionResult(const std::string &s, amp::DetectionResult &out) {
        try {
            json j = json::parse(s);
            out = j.get<amp::DetectionResult>();
            return true;
        } catch (...) {
            return false;
        }
    }
};
} // namespace onnx
