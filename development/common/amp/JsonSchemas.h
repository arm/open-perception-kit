#pragma once

#include "amp/Shape.h"
#include "amp/Types.h"

#include <nlohmann/json.hpp>
#include <vector>

using nlohmann::json;

namespace amp {

// --- Serialization ---
inline void to_json(json &j, const Colorf &c) {
    // Object form (human-readable)
    j = json{{"r", c.r}, {"g", c.g}, {"b", c.b}, {"a", c.a}};
}

// --- Deserialization ---
inline void from_json(const json &j, Colorf &c) {
    if (j.is_object()) {
        // Required: r,g,b. Optional: a (defaults to 1.0f)
        j.at("r").get_to(c.r);
        j.at("g").get_to(c.g);
        j.at("b").get_to(c.b);

        if (j.contains("a") && !j.at("a").is_null()) {
            j.at("a").get_to(c.a);
        } else {
            c.a = 1.0f;
        }
        return;
    }

    if (j.is_array()) {
        // Accept [r,g,b] or [r,g,b,a]
        if (j.size() != 3 && j.size() != 4) {
            throw std::runtime_error("Colorf array must have 3 or 4 elements");
        }
        c.r = j.at(0).get<float>();
        c.g = j.at(1).get<float>();
        c.b = j.at(2).get<float>();
        c.a = (j.size() == 4) ? j.at(3).get<float>() : 1.0f;
        return;
    }

    throw std::runtime_error("Colorf must be a JSON object or array");
}

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
