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
#include <vector>

namespace opk {
// Declared capability, not an assertion that inference ran or returned items.
struct OutputCapability {
    std::string payloadType;
    std::string role;
    bool operator==(const OutputCapability &) const = default;
};
template <typename Payload> OutputCapability outputCapability(const std::string_view role = {}) {
    return {std::string(open_perception_kit::detail::native_traits<Payload>::qualified_root_type),
            std::string(role)};
}
inline bool provides(const OutputCapability &output, const OutputCapability &requirement) {
    return !requirement.payloadType.empty() && output.payloadType == requirement.payloadType &&
           (requirement.role.empty() || output.role == requirement.role);
}
} // namespace opk
