/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pek/AttributeMap.h"
#include "pek/Result.h"

#include "pek/JsonSchemas.h"

#include <nlohmann/json.hpp>

namespace pek::op {

inline bool isInferenceOpId(std::string_view id) {
    constexpr std::string_view Suffix = "/Inference";
    return id.size() > Suffix.size() && id.ends_with(Suffix) &&
           id.find('/') == id.size() - Suffix.size();
}

/**
 * @brief Descriptor for a chain of operations loaded from JSON.
 *
 * OpChainDescriptor defines the structure and configuration of an operation chain.
 * It is typically loaded from a JSON file and used to construct an OpChain at runtime.
 * Each operation in the descriptor specifies its library, name, optional loop ID,
 * and configuration attributes.
 */
struct OpChainDescriptor {

    /**
     * @brief Descriptor for a single operation in the chain.
     */
    struct Op {
        std::string id; ///< Unique identifier combining library and operation name (e.g.,
                        ///< "libName/opName").
        std::optional<std::size_t>
            loopId; ///< Optional loop group ID; operations with equal IDs form a loop.
        AttributeMap
            attributes; ///< Configuration attributes passed to the operation's configure() method.
    };

    std::string name;        ///< Internal name of the operation chain.
    std::string description; ///< Human-readable description of the operation chain.
    std::string displayName; ///< Optional user-facing model name.
    std::string task;        ///< Optional user-facing task description.
    std::string runtime;     ///< Optional user-facing inference runtime.

    std::vector<Op> ops; ///< List of operations in the chain, in execution order.

    /**
     * @brief Parses an OpChainDescriptor from a JSON string.
     *
     * @param jsonString JSON string containing a descriptor object with "name" and "ops" fields.
     * @param source Descriptor source path used for diagnostics and filename routing.
     * @return OpChainDescriptor on success, or an error Result.
     */
    static pek::Result<OpChainDescriptor> fromJson(const std::string &jsonString,
                                                   const std::string &source = "opchain.json");
    /**
     * @brief Loads an OpChainDescriptor from a JSON file.
     *
     * @param path File path to the JSON descriptor file.
     * @return OpChainDescriptor with relative Inference modelDescriptor paths joined to path
     * without lexical normalization, or an error Result. Absolute values are preserved.
     */
    static pek::Result<OpChainDescriptor> fromFile(const std::string &path);
};

} // namespace pek::op

// ---

#include <nlohmann/json.hpp>

namespace pek::op {

// ---- Op ----

inline void to_json(nlohmann::json &j, const OpChainDescriptor::Op &op) {
    j = nlohmann::json{{"id", op.id}, {"attributes", op.attributes}};
    if (op.loopId.has_value())
        j["loopId"] = *op.loopId;
}

inline void from_json(const nlohmann::json &j, OpChainDescriptor::Op &op) {
    j.at("id").get_to(op.id);
    if (j.contains("loopId"))
        op.loopId = j.at("loopId").get<std::size_t>();
    else
        op.loopId.reset();
    if (j.contains("attributes"))
        j.at("attributes").get_to(op.attributes);
    else
        op.attributes.clear();
}

// ---- OpChainDescriptor ----

inline void to_json(nlohmann::json &j, const OpChainDescriptor &desc) {
    j = nlohmann::json{
        {"version", 1}, {"name", desc.name}, {"description", desc.description}, {"ops", desc.ops}};
    if (!desc.displayName.empty())
        j["displayName"] = desc.displayName;
    if (!desc.task.empty())
        j["task"] = desc.task;
    if (!desc.runtime.empty())
        j["runtime"] = desc.runtime;
}

inline void from_json(const nlohmann::json &j, OpChainDescriptor &desc) {
    j.at("name").get_to(desc.name);
    j.at("description").get_to(desc.description);
    j.at("ops").get_to(desc.ops);
    if (j.contains("displayName"))
        j.at("displayName").get_to(desc.displayName);
    if (j.contains("task"))
        j.at("task").get_to(desc.task);
    if (j.contains("runtime"))
        j.at("runtime").get_to(desc.runtime);
}

} // namespace pek::op
