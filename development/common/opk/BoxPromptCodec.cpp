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

#include "opk/BoxPromptCodec.h"

#include "opk/BoxPromptInputs.h"

#include <utility>

namespace opk {
namespace {

class BoxPromptCodec final : public PromptCodec {
  public:
    Result<PromptSnapshot> decode(const std::span<const uint8_t> bytes,
                                  const std::string &session,
                                  const std::string &inferenceInstance,
                                  const std::string &sourceName,
                                  uint64_t &requestId,
                                  uint64_t &revision) const override {
        auto decoded = decodeBoxPrompt(bytes);
        if (!decoded) {
            return tl::unexpected(decoded.error());
        }
        if (decoded->request) {
            requestId = decoded->request->request_id;
            revision = decoded->request->revision;
        }
        return makeBoxPromptSnapshot(session, std::move(*decoded), inferenceInstance, sourceName);
    }

    Result<void> validate(const PromptSnapshot &snapshot,
                          const std::string &inferenceInstance,
                          const std::string &sourceName) const override {
        return validateBoxPrompt(snapshot, inferenceInstance, sourceName);
    }
};

} // namespace

const PromptCodec &boxPromptCodec() {
    static const BoxPromptCodec codec;
    return codec;
}

} // namespace opk
