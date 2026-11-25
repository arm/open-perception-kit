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

#include "opk/PromptCodec.h"

namespace opk {

struct PromptCodecCapability {
    PromptFormat format;
    size_t maxSerializedBytes;
    PromptLimits limits;
    size_t maxGroupNameBytes;
    const PromptCodec *codec;
};

/// Decoder capabilities, not a promise that a destination model consumes every format.
std::span<const PromptCodecCapability> promptCodecCapabilities();
const PromptCodecCapability *findPromptCodec(const std::string_view identifier);
const PromptCodecCapability *findPromptCodec(const std::span<const uint8_t> bytes);

} // namespace opk
