#pragma once

#include <string>
#include <vector>

#include "amp/AttributeMap.h"

namespace amp {

struct OpChainDescriptor {

    struct Op {
        std::string id;
        AttributeMap attributes;
    };

    std::vector<Op> ops;
};

} // namespace amp

// ---

#include <nlohmann/json.hpp>

namespace amp {

// ---- Op ----

inline void to_json(nlohmann::json &j, const OpChainDescriptor::Op &op) {
    j = nlohmann::json{{"id", op.id}, {"attributes", op.attributes}};
}

inline void from_json(const nlohmann::json &j, OpChainDescriptor::Op &op) {
    j.at("id").get_to(op.id);
    if (j.contains("attributes"))
        j.at("attributes").get_to(op.attributes);
}

// ---- OpChainDescriptor ----

inline void to_json(nlohmann::json &j, const OpChainDescriptor &desc) {
    j = nlohmann::json{{"ops", desc.ops}};
}

inline void from_json(const nlohmann::json &j, OpChainDescriptor &desc) {
    j.at("ops").get_to(desc.ops);
}

} // namespace amp