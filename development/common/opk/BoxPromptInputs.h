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

#include "opk/PromptInputs.h"
#include <span>

namespace opk {

inline constexpr size_t MaxBoxPromptRequestBytes = 4096;
inline constexpr PromptFormat BoxPromptFormat{"BPRM", 1, 0};

class BoxPromptPayload final : public PromptPayload {
  public:
    explicit BoxPromptPayload(const open_perception_kit::metadata::PromptBoxT &box);
    ~BoxPromptPayload() override;
    PromptFormat format() const override;
    size_t ownedBytes() const override;

    const open_perception_kit::metadata::PromptBoxT box;
};

Result<open_perception_kit::metadata::BoxPromptInputsT>
decodeBoxPrompt(const std::span<const uint8_t> bytes);

Result<void> validateBoxPrompt(const open_perception_kit::metadata::BoxPromptInputsT &request,
                               const std::string &inferenceInstance,
                               const std::string &sourceName);
Result<void> validateBoxPrompt(const PromptSnapshot &snapshot,
                               const std::string &inferenceInstance,
                               const std::string &sourceName);

/// Adapts the existing wire root into an owning runtime envelope and typed payload.
Result<PromptSnapshot>
makeBoxPromptSnapshot(const std::string &session,
                      open_perception_kit::metadata::BoxPromptInputsT request,
                      const std::string &inferenceInstance,
                      const std::string &sourceName);

} // namespace opk
