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

// Initial host safety bounds, independent of device tuning and the 4096-byte box protocol.
inline constexpr size_t MaxQueryPromptGroups = 16;
inline constexpr size_t MaxPromptGroupNameBytes = 128;
inline constexpr size_t MaxPromptDescriptionBytes = 1024;
inline constexpr size_t MaxPromptReferences = 4;
inline constexpr uint32_t MaxPromptReferenceDimension = 2048;
inline constexpr size_t MaxPromptReferenceBytes = 16 * 1024 * 1024;
inline constexpr size_t MaxTextPromptRequestBytes = 64 * 1024;
inline constexpr size_t MaxVisualPromptRequestBytes = MaxPromptReferenceBytes + 64 * 1024;
inline constexpr PromptFormat TextPromptFormat{"TPRM", 1, 0};
inline constexpr PromptFormat VisualPromptFormat{"VPRM", 1, 0};

Result<PromptSnapshot> decodeTextPrompt(const std::span<const uint8_t> bytes,
                                        const std::string &session,
                                        const std::string &inferenceInstance,
                                        const std::string &sourceName,
                                        uint64_t &requestId,
                                        uint64_t &revision);

/// Const generated views borrow this immutable owner; do not retain a view without its owner.
class TextPromptPayload final : public PromptPayload {
  public:
    PromptFormat format() const override;
    const open_perception_kit::metadata::TextPromptPayload &value() const;
    size_t ownedBytes() const override;

  private:
    explicit TextPromptPayload(const std::span<const uint8_t> bytes);
    const std::vector<uint8_t> storage;
    friend Result<PromptSnapshot> decodeTextPrompt(const std::span<const uint8_t> bytes,
                                                   const std::string &session,
                                                   const std::string &inferenceInstance,
                                                   const std::string &sourceName,
                                                   uint64_t &requestId,
                                                   uint64_t &revision);
};

Result<void> validateTextPrompt(const PromptSnapshot &snapshot,
                                const std::string &inferenceInstance,
                                const std::string &sourceName);

Result<PromptSnapshot> decodeVisualPrompt(const std::span<const uint8_t> bytes,
                                          const std::string &session,
                                          const std::string &inferenceInstance,
                                          const std::string &sourceName,
                                          uint64_t &requestId,
                                          uint64_t &revision);

/// Const generated views borrow this immutable owner; do not retain a view without its owner.
class VisualPromptPayload final : public PromptPayload {
  public:
    PromptFormat format() const override;
    const open_perception_kit::metadata::VisualPromptPayload &value() const;
    size_t ownedBytes() const override;

  private:
    explicit VisualPromptPayload(const std::span<const uint8_t> bytes);
    const std::vector<uint8_t> storage;
    friend Result<PromptSnapshot> decodeVisualPrompt(const std::span<const uint8_t> bytes,
                                                     const std::string &session,
                                                     const std::string &inferenceInstance,
                                                     const std::string &sourceName,
                                                     uint64_t &requestId,
                                                     uint64_t &revision);
};

Result<void> validateVisualPrompt(const PromptSnapshot &snapshot,
                                  const std::string &inferenceInstance,
                                  const std::string &sourceName);

} // namespace opk
