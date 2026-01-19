#pragma once

#include "amp/PerceptionContext.h"
#include "amp/Shape.h"
#include "amp/Types.h"

#include <nlohmann/json.hpp>
#include <vector>

using nlohmann::json;

namespace amp {

inline void to_json(json &j, const DetectionRect &b) {
    j = json{{"x", b.x},
             {"y", b.y},
             {"w", b.w},
             {"h", b.h},
             {"confidence", b.confidence},
             {"classIndex", b.classIndex}};
}

inline void from_json(const json &j, DetectionRect &b) {
    j.at("x").get_to(b.x);
    j.at("y").get_to(b.y);
    j.at("w").get_to(b.w);
    j.at("h").get_to(b.h);
    j.at("confidence").get_to(b.confidence);
    j.at("classIndex").get_to(b.classIndex);
}

inline void to_json(json &j, const DetectionPoint &p) {
    j = json{{"x", p.x}, {"y", p.y}};
}

inline void from_json(const json &j, DetectionPoint &p) {
    j.at("x").get_to(p.x);
    j.at("y").get_to(p.y);
}

inline void to_json(json &j, const DetectionResult &r) {
    j = json{{"inferId", r.inferId},
             {"originTs", r.originTs},
             {"inferTs", r.inferTs},
             {"rects", r.rects},
             {"points", r.points}};
}

inline void from_json(const json &j, DetectionResult &r) {
    j.at("inferId").get_to(r.inferId);
    j.at("originTs").get_to(r.originTs);
    j.at("inferTs").get_to(r.inferTs);
    j.at("rects").get_to(r.rects);
    j.at("points").get_to(r.points);
}

} // namespace amp

namespace amp {

inline void to_json(json &j, const amp::Shape &s) {
    j = json::array();
    for (size_t i = 0; i < s.dimensionCount; ++i) {
        j.push_back(s.valueCount[i]);
    }
}

inline void from_json(const json &j, amp::Shape &s) {
    // Expect an array of integers
    if (!j.is_array()) {
        throw std::runtime_error("Shape must be a JSON array");
    }

    s.dimensionCount = 0;
    std::fill(std::begin(s.valueCount), std::end(s.valueCount), 0);

    size_t i = 0;
    for (const auto &v : j) {
        if (i >= 8) {
            throw std::runtime_error("Too many dimensions for Shape (max 8)");
        }
        s.valueCount[i] = v.get<size_t>();
        ++i;
    }
    s.dimensionCount = i;
}

NLOHMANN_JSON_SERIALIZE_ENUM(amp::DataKind,
                             {
                                 {DataKind::ImageRgbChw, "ImageRgbChw"},
                                 {DataKind::ImageRgbHwc, "ImageRgbHwc"},
                                 {DataKind::ImageGray, "ImageGray"},
                                 {DataKind::Value, "Value"},
                                 {DataKind::Vector2, "Vector2"},
                                 {DataKind::Vector3, "Vector3"},
                                 {DataKind::Vector4, "Vector4"},
                             })
} // namespace amp
