/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "opk/Shape.h"
#include "opk/Types.h"

#include <nlohmann/json.hpp>

#include <limits>
#include <string>
#include <vector>

using nlohmann::json;

namespace opk {

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

inline void to_json(json &j, const opk::Shape &s) {
    j = json::array();
    for (size_t i = 0; i < s.rank; ++i) {
        j.push_back(s.dims[i]);
    }
}

inline void from_json(const json &j, opk::Shape &s) {
    // Expect an array of integers
    if (!j.is_array()) {
        throw std::runtime_error("Shape must be a JSON array");
    }

    s.rank = 0;
    std::fill(std::begin(s.dims), std::end(s.dims), 0);

    size_t i = 0;
    for (const auto &v : j) {
        if (i >= 8) {
            throw std::runtime_error("Too many dimensions for Shape (max 8)");
        }
        const int64_t dim = v.get<int64_t>();
        if (dim == 0 || dim < -1 || dim > std::numeric_limits<int>::max()) {
            throw std::runtime_error("Shape dimension must be -1 or a positive int");
        }
        s.dims[i] = static_cast<int>(dim);
        ++i;
    }
    s.rank = i;
}

NLOHMANN_JSON_SERIALIZE_ENUM(opk::DataKind,
                             {
                                 {DataKind::ImageRgbChw, "ImageRgbChw"},
                                 {DataKind::ImageRgbHwc, "ImageRgbHwc"},
                                 {DataKind::ImageGray, "ImageGray"},
                                 {DataKind::RawTensorData, "RawTensorData"},
                                 {DataKind::Value, "Value"},
                                 {DataKind::Vector2, "Vector2"},
                                 {DataKind::Vector3, "Vector3"},
                                 {DataKind::Vector4, "Vector4"},
                             })

inline void to_json(json &j, const opk::Dtype &t) {
    switch (t) {
    case opk::Dtype::Uint8:
        j = "Uint8";
        return;
    case opk::Dtype::Int8:
        j = "Int8";
        return;
    case opk::Dtype::Float16:
        j = "Float16";
        return;
    case opk::Dtype::Float32:
        j = "Float32";
        return;
    case opk::Dtype::Int64:
        j = "Int64";
        return;
    default:
        j = "Float32";
        return;
    }
}

inline void from_json(const json &j, opk::Dtype &t) {
    if (!j.is_string()) {
        throw json::type_error::create(302, "Dtype must be a canonical JSON string", &j);
    }

    const std::string s = j.get<std::string>();

    if (s == "Uint8") {
        t = opk::Dtype::Uint8;
        return;
    }
    if (s == "Int8") {
        t = opk::Dtype::Int8;
        return;
    }
    if (s == "Float16") {
        t = opk::Dtype::Float16;
        return;
    }
    if (s == "Float32") {
        t = opk::Dtype::Float32;
        return;
    }
    if (s == "Int64") {
        t = opk::Dtype::Int64;
        return;
    }

    throw std::runtime_error("Unknown Dtype value");
}
} // namespace opk

// ---

namespace opk {

inline void to_json(nlohmann::json &j, const TensorFeedback &v) {
    j = nlohmann::json{
        {"mode", "Copy"},
        {"fromOutputTensorIndex", v.fromOutputTensorIndex},
        {"toInputTensorIndex", v.toInputTensorIndex},
    };
}

inline void from_json(const nlohmann::json &j, TensorFeedback &v) {
    if (j.value("mode", std::string{}) != "Copy" || !j.contains("fromOutputTensorIndex") ||
        !j.contains("toInputTensorIndex")) {
        throw nlohmann::json::type_error::create(
            302, "ModelTensorFeedback requires mode Copy and both tensor indices", &j);
    }

    v.fromOutputTensorIndex = j.at("fromOutputTensorIndex").get<size_t>();
    v.toInputTensorIndex = j.at("toInputTensorIndex").get<size_t>();
}

} // namespace opk
