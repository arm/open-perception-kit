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

#include "opk/OutputCapability.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <optional>

namespace opk {

inline bool validConnectionText(const std::string &value) {
    return value.size() <= 1024 &&
           !std::ranges::any_of(value, [](const unsigned char c) { return c < 32 || c == 127; });
}

inline std::optional<std::vector<InputBinding>> parseInputBindings(const char *const text) {
    const std::string_view encoded = text ? text : "[]";
    if (encoded.size() > 65536)
        return std::nullopt;
    const auto value = nlohmann::json::parse(encoded, nullptr, false);
    if (!value.is_array() || value.size() > 128)
        return std::nullopt;
    std::vector<InputBinding> result;
    for (const auto &entry : value) {
        if (!entry.is_object() || entry.size() != 4)
            return std::nullopt;
        for (const char *const field : {"consumerId", "inputId", "payloadType", "producerId"}) {
            if (!entry.contains(field) || !entry[field].is_string() ||
                !validConnectionText(entry[field].get_ref<const std::string &>()))
                return std::nullopt;
        }
        InputBinding binding{entry["consumerId"].get<std::string>(),
                             entry["inputId"].get<std::string>(),
                             entry["payloadType"].get<std::string>(),
                             entry["producerId"].get<std::string>()};
        if (binding.consumerId.empty() || binding.inputId.empty() ||
            !isKnownPayloadType(binding.payloadType) ||
            std::ranges::any_of(result, [&](const auto &previous) {
                return previous.consumerId == binding.consumerId &&
                       previous.inputId == binding.inputId;
            }))
            return std::nullopt;
        result.push_back(std::move(binding));
    }
    return result;
}

inline std::optional<std::vector<std::string>> parseProducerIds(const char *const text) {
    const std::string_view encoded = text ? text : "[]";
    if (encoded.size() > 65536)
        return std::nullopt;
    const auto value = nlohmann::json::parse(encoded, nullptr, false);
    if (!value.is_array() || value.size() > 128)
        return std::nullopt;
    std::vector<std::string> result;
    for (const auto &entry : value) {
        if (!entry.is_string())
            return std::nullopt;
        const auto &id = entry.get_ref<const std::string &>();
        if (id.empty() || !validConnectionText(id) || std::ranges::find(result, id) != result.end())
            return std::nullopt;
        result.push_back(id);
    }
    return result;
}

} // namespace opk
