/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once
#include "open_perception_kit.h"
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace opk {
// Declared capability, not an assertion that inference ran or returned items.
struct OutputCapability {
    std::string payloadType;
    bool operator==(const OutputCapability &) const = default;
};
template <typename Payload> OutputCapability outputCapability() {
    return {std::string(open_perception_kit::detail::native_traits<Payload>::qualified_root_type)};
}

inline bool isKnownPayloadType(const std::string_view name) {
    using Payloads = open_perception_kit::detail::payload_data;
    return [&]<std::size_t... Index>(std::index_sequence<Index...>) {
        return (
            ([&] {
                using Payload = std::variant_alternative_t<Index, Payloads>;
                if constexpr (open_perception_kit::container::native_payload<Payload>)
                    return name ==
                           open_perception_kit::detail::native_traits<Payload>::qualified_root_type;
                return false;
            }()) ||
            ...);
    }(std::make_index_sequence<std::variant_size_v<Payloads>>{});
}

// A concrete output address. The producer ID is the instance_id carried in metadata,
// not a semantic label, parser name or a wildcard selecting a family of producers.
struct OutputReference {
    std::string payloadType;
    std::string producerId;
    bool operator==(const OutputReference &) const = default;
};

// Authored input: sourceOp names one earlier operation in the same chain;
// otherwise producerId is an external address (empty means explicitly unbound).
struct InputReference {
    std::string inputId;
    std::string payloadType;
    std::string producerId;
    std::string sourceOp;
    bool operator==(const InputReference &) const = default;
};

// Runtime connection for one consumer/type input, including model-selector changes.
struct InputBinding {
    std::string consumerId;
    std::string inputId;
    std::string payloadType;
    std::string producerId;
    bool operator==(const InputBinding &) const = default;
};

inline std::string producerInstanceId(const std::string_view inferId,
                                      const std::string_view instanceId) {
    return std::string(inferId) + "/" + std::string(instanceId);
}

inline std::string resolveInputProducerId(const InputReference &input,
                                          const std::string_view consumerId,
                                          const std::string_view inferId,
                                          const std::vector<InputBinding> &bindings) {
    if (!input.sourceOp.empty())
        return producerInstanceId(inferId, input.sourceOp);
    const InputBinding *selected = nullptr;
    for (const auto &binding : bindings) {
        if (binding.consumerId == consumerId && binding.inputId == input.inputId) {
            if (binding.payloadType != input.payloadType)
                return {};
            if (selected)
                return {}; // Ambiguity must not choose an arbitrary producer.
            selected = &binding;
        }
    }
    return selected ? selected->producerId : input.producerId;
}

inline bool provides(const OutputReference &output, const OutputReference &input) {
    return !input.payloadType.empty() && !input.producerId.empty() && output == input;
}
} // namespace opk
