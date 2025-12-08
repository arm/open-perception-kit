#pragma once

#include <cstdint>
#include <onnxruntime_cxx_api.h>

#include "amp/Result.h"
#include "uniflow/detection_types.h"
#include "uniflow/fixed_string.h"
#include "uniflow/public_types.h"
#include "uniflow/uniflow.h"
#include "uniflow/yolo_like_parser.h"
#include "uniflow/model_io.h"

#include "JsonSchemas.h" 

#include <vector>
#include <map>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace onnx {

enum class Result {
    Ok = 0,
    UniflowModelInspectError,
    CreateEnvironmentError,
    TensorProblem
};

struct Tools {

    static bool onnxTypeToUniflowType(ONNXTensorElementDataType onnxType, uflw::ValueType& outUniflowType);
    static bool getTensorShape(const Ort::Session& session, uflw::TensorInOut tensorInOut, int inputIndex, uflw::Shape& outShape);

    static uflw::ModelFamily guessModelFamily(const Ort::Session& session, uflw::FxString<32>& outVersion);
    static uflw::TensorDataKind guessModelInputDataKind(const Ort::Session& session, int inputIndex, int& outBatchCount);
    static std::map<std::string, std::string> getModelMeta(const Ort::Session& session);

    static amp::Result<uflw::Model> inspectModel(const Ort::Session& session);


    static std::string toString(const uflw::Model& model);

    // ---

    static std::string serializeDetectionResult(const uflw::DetectionResult& r) {
        json j = r;
        return j.dump();
    }

    static bool deserializeDetectionResult(const std::string& s, uflw::DetectionResult& out) {
        try {
            json j = json::parse(s);
            out = j.get<uflw::DetectionResult>();
            return true;
        } catch (...) {
            return false;
        }
    }
    
};}



