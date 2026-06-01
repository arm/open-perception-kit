/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#pragma once

#include <string>
#include <vector>

#include "pek/AttributeMap.h"
#include "pek/Result.h"

#include "pek/JsonSchemas.h"

#include <nlohmann/json.hpp>
#include <string>

namespace pek::op {

/**
 * @brief Descriptor for a chain of operations loaded from JSON.
 *
 * OpChainDescriptor defines the structure and configuration of an operation chain.
 * It is typically loaded from a JSON file and used to construct an OpChain at runtime.
 * Each operation in the descriptor specifies its library, name, optional group, loop ID,
 * and configuration attributes.
 */
struct OpChainDescriptor {

    /**
     * @brief Descriptor for a single operation in the chain.
     */
    struct Op {
        std::string id; ///< Unique identifier combining library and operation name (e.g.,
                        ///< "libName/opName").
        std::string
            group; ///< Optional group identifier for operations that should be executed together.
        size_t loopId =
            0; ///< Optional loop group ID; operations with equal non-zero loopId form a loop.
        AttributeMap
            attributes; ///< Configuration attributes passed to the operation's configure() method.
    };

    std::string name; ///< Name of the operation chain.

    std::vector<Op> ops; ///< List of operations in the chain, in execution order.

    /**
     * @brief Parses an OpChainDescriptor from a JSON string.
     *
     * @param jsonString JSON string containing a descriptor object with "name" and "ops" fields.
     * @return OpChainDescriptor on success, or an error Result.
     */
    static pek::Result<OpChainDescriptor> fromJson(const std::string &jsonString);
    /**
     * @brief Loads an OpChainDescriptor from a JSON file.
     *
     * @param path File path to the JSON descriptor file.
     * @return OpChainDescriptor on success, or an error Result.
     */
    static pek::Result<OpChainDescriptor> fromFile(const std::string &path);
};

} // namespace pek::op

// ---

#include <nlohmann/json.hpp>

namespace pek::op {

// ---- Op ----

inline void to_json(nlohmann::json &j, const OpChainDescriptor::Op &op) {
    j = nlohmann::json{
        {"id", op.id}, {"group", op.group}, {"loopId", op.loopId}, {"attributes", op.attributes}};
}

inline void from_json(const nlohmann::json &j, OpChainDescriptor::Op &op) {
    j.at("id").get_to(op.id);
    if (j.contains("group"))
        j.at("group").get_to(op.group);
    if (j.contains("loopId"))
        j.at("loopId").get_to(op.loopId);

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

} // namespace pek::op