/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Shape.h"
#include "pek/Types.h"

#include "magic_enum/magic_enum.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <vector>

using nlohmann::json;

namespace pek {

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

inline void to_json(json &j, const pek::Shape &s) {
    j = json::array();
    for (size_t i = 0; i < s.rank; ++i) {
        j.push_back(s.dims[i]);
    }
}

inline void from_json(const json &j, pek::Shape &s) {
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
        s.dims[i] = v.get<size_t>();
        ++i;
    }
    s.rank = i;
}

NLOHMANN_JSON_SERIALIZE_ENUM(pek::DataKind,
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

inline void to_json(json &j, const pek::Dtype &t) {
    switch (t) {
    case pek::Dtype::Uint8:
        j = "Uint8";
        return;
    case pek::Dtype::Int8:
        j = "Int8";
        return;
    case pek::Dtype::Float16:
        j = "Float16";
        return;
    case pek::Dtype::Float32:
        j = "Float32";
        return;
    case pek::Dtype::Int64:
        j = "Int64";
        return;
    default:
        j = "Float32";
        return;
    }
}

inline void from_json(const json &j, pek::Dtype &t) {
    if (j.is_number_integer()) {
        t = static_cast<pek::Dtype>(j.get<int>());
        return;
    }

    if (!j.is_string()) {
        throw std::runtime_error("Dtype must be a JSON string or integer");
    }

    std::string s = j.get<std::string>();
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (s == "uint8" || s == "u8") {
        t = pek::Dtype::Uint8;
        return;
    }
    if (s == "int8" || s == "i8") {
        t = pek::Dtype::Int8;
        return;
    }
    if (s == "float16" || s == "f16") {
        t = pek::Dtype::Float16;
        return;
    }
    if (s == "float32" || s == "f32" || s == "float") {
        t = pek::Dtype::Float32;
        return;
    }
    if (s == "int64" || s == "i64") {
        t = pek::Dtype::Int64;
        return;
    }

    throw std::runtime_error("Unknown Dtype value");
}
} // namespace pek

// ---

namespace pek {

inline void to_json(nlohmann::json &j, const TensorFeedback &v) {
    // compact + explicit
    j = nlohmann::json{
        {"mode", std::string(magic_enum::enum_name(TensorFeedback::Mode::Copy))},
        {"fromOutputTensorIndex", v.fromOutputTensorIndex},
        {"toInputTensorIndex", v.toInputTensorIndex},
    };
}

inline void from_json(const nlohmann::json &j, TensorFeedback &v) {
    // kind is optional today (since only Copy exists), but we validate if present
    if (auto it = j.find("kind"); it != j.end() && !it->is_null()) {
        const std::string s = it->get<std::string>();
        const auto k = magic_enum::enum_cast<TensorFeedback::Mode>(s);
        if (!k) {
            throw std::runtime_error("ModelTensorFeedback.kind: unknown value '" + s + "'");
        }
        if (*k != TensorFeedback::Mode::Copy) {
            throw std::runtime_error("ModelTensorFeedback.kind: unsupported value '" + s + "'");
        }
    }

    if (!j.contains("fromOutputTensorIndex") || !j.contains("toInputTensorIndex")) {
        throw std::runtime_error("ModelTensorFeedback: missing required fields");
    }

    v.fromOutputTensorIndex = j.at("fromOutputTensorIndex").get<size_t>();
    v.toInputTensorIndex = j.at("toInputTensorIndex").get<size_t>();
}

} // namespace pek