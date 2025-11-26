#pragma once

#include <onnxruntime_cxx_api.h>

#include "uniflow/detection_types.h"
#include "uniflow/fixed_string.h"
#include "uniflow/public_types.h"
#include "uniflow/uniflow.h"
#include "uniflow/yolo_like_parser.h"
#include "uniflow/model_io.h"

#include <vector>

#include <nlohmann/json.hpp>
#include <vector>  // important

using nlohmann::json;

enum class OnnxResult {
    Ok = 0,
    UniflowModelInspectError,
    CreateEnvironmentError
};

namespace uflw {

    inline void to_json(json& j, const DetectionRect& b) {
        j = json {
            { "x", b.x },
            { "y", b.y },
            { "w", b.w },
            { "h", b.h },
            { "confidence", b.confidence },
            { "classIndex", b.classIndex }
        };
    }

    inline void from_json(const json& j, DetectionRect& b) {
        j.at("x").get_to(b.x);
        j.at("y").get_to(b.y);
        j.at("w").get_to(b.w);
        j.at("h").get_to(b.h);
        j.at("confidence").get_to(b.confidence);
        j.at("classIndex").get_to(b.classIndex);
    }

    inline void to_json(json& j, const DetectionPoint& p) {
        j = json { { "x", p.x }, { "y", p.y } };
    }

    inline void from_json(const json& j, DetectionPoint& p) {
        j.at("x").get_to(p.x);
        j.at("y").get_to(p.y);
    }

    inline void to_json(json& j, const DetectionResult& r) {
        j = json {
            { "inferId",  r.inferId },
            { "originTs", r.originTs },
            { "inferTs",  r.inferTs },
            { "rects",    r.rects },
            { "points",   r.points }
        };
    }

    inline void from_json(const json& j, DetectionResult& r) {
        j.at("inferId").get_to(r.inferId);
        j.at("originTs").get_to(r.originTs);
        j.at("inferTs").get_to(r.inferTs);
        j.at("rects").get_to(r.rects);
        j.at("points").get_to(r.points);
    }

} // namespace uflw

struct OnnxTools {

    static bool onnxTypeToUniflowType(ONNXTensorElementDataType onnxType, uflw::ValueType& outUniflowType);
    static bool getTensorShape(const Ort::Session& session, uflw::TensorInOut tensorInOut, int inputIndex, uflw::Shape& outShape);

    static uflw::ModelFamily guessModelFamily(const Ort::Session& session, uflw::FxString<32>& outVersion);
    static uflw::InputTensorDataKind guessModelInputDataKind(const Ort::Session& session, int inputIndex, int& outBatchCount);

    static uflw::Model inspectModel(const Ort::Session& session);

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
};
