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

#include "opk/PromptCodecs.h"
#include "opk/BoxPromptCodec.h"
#include "opk/BoxPromptInputs.h"
#include "opk/QueryPromptInputs.h"

#include <array>

namespace opk {
namespace {

template <auto Decode, auto Validate> class QueryCodec final : public PromptCodec {
  public:
    Result<PromptSnapshot> decode(const std::span<const uint8_t> bytes,
                                  const std::string &session,
                                  const std::string &inferenceInstance,
                                  const std::string &sourceName,
                                  uint64_t &requestId,
                                  uint64_t &revision) const override {
        return Decode(bytes, session, inferenceInstance, sourceName, requestId, revision);
    }
    Result<void> validate(const PromptSnapshot &snapshot,
                          const std::string &inferenceInstance,
                          const std::string &sourceName) const override {
        return Validate(snapshot, inferenceInstance, sourceName);
    }
};
} // namespace

std::span<const PromptCodecCapability> promptCodecCapabilities() {
    static const QueryCodec<decodeTextPrompt, validateTextPrompt> text;
    static const QueryCodec<decodeVisualPrompt, validateVisualPrompt> visual;
    static const std::array entries{
        PromptCodecCapability{BoxPromptFormat, MaxBoxPromptRequestBytes, {}, 0, &boxPromptCodec()},
        PromptCodecCapability{TextPromptFormat,
                              MaxTextPromptRequestBytes,
                              {MaxQueryPromptGroups, 0, 0, 0, MaxPromptDescriptionBytes},
                              MaxPromptGroupNameBytes,
                              &text},
        PromptCodecCapability{VisualPromptFormat,
                              MaxVisualPromptRequestBytes,
                              {MaxQueryPromptGroups,
                               MaxPromptReferences,
                               MaxPromptReferenceDimension,
                               MaxPromptReferenceBytes},
                              MaxPromptGroupNameBytes,
                              &visual},
    };
    return entries;
}

const PromptCodecCapability *findPromptCodec(const std::string_view identifier) {
    for (const auto &entry : promptCodecCapabilities()) {
        if (entry.format.id == identifier) {
            return &entry;
        }
    }
    return nullptr;
}

const PromptCodecCapability *findPromptCodec(const std::span<const uint8_t> bytes) {
    // FlatBuffers root offset occupies the first four bytes, then the file identifier.
    if (bytes.size() < 2 * sizeof(flatbuffers::uoffset_t)) {
        return nullptr;
    }
    return findPromptCodec(std::string_view(reinterpret_cast<const char *>(bytes.data() + 4), 4));
}
} // namespace opk
