/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#pragma once

#include <string>
#include <vector>

#include "amp/AttributeMap.h"
#include "amp/Result.h"

#include "amp/JsonSchemas.h"

#include <nlohmann/json.hpp>
#include <string>

namespace amp {

struct OpChainDescriptor {

    struct Op {
        std::string id, group;
        AttributeMap attributes;
    };

    std::string name;

    std::vector<Op> ops;

    static amp::Result<OpChainDescriptor> fromJson(const std::string &jsonString);
    static amp::Result<OpChainDescriptor> fromFile(const std::string &path);
};

} // namespace amp

// ---

#include <nlohmann/json.hpp>

namespace amp {

// ---- Op ----

inline void to_json(nlohmann::json &j, const OpChainDescriptor::Op &op) {
    j = nlohmann::json{{"id", op.id}, {"group", op.group}, {"attributes", op.attributes}};
}

inline void from_json(const nlohmann::json &j, OpChainDescriptor::Op &op) {
    j.at("id").get_to(op.id);
    if (j.contains("group"))
        j.at("group").get_to(op.group);
    if (j.contains("attributes"))
        j.at("attributes").get_to(op.attributes);
}

// ---- OpChainDescriptor ----

inline void to_json(nlohmann::json &j, const OpChainDescriptor &desc) {
    j = nlohmann::json{{"name", desc.name}, {"ops", desc.ops}};
}

inline void from_json(const nlohmann::json &j, OpChainDescriptor &desc) {
    if (j.contains("name") && j["name"].is_string()) {
        desc.name = j["name"].get<std::string>();
    } else {
        desc.name = "unknown";
    }
    j.at("ops").get_to(desc.ops);
}

} // namespace amp