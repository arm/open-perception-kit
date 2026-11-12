/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

#ifndef OPK_SILERO_VAD_PARSER_H
#define OPK_SILERO_VAD_PARSER_H
#include "opk/TensorParser.h"

namespace opk::stdop::postproc {

/// Parses Silero's single [1,1] Float32 speech probability.
/// Attack/release durations use 16 kHz audio sample counts, never wall time.
struct SileroVadParser : public opk::TensorParser {
    std::vector<opk::OutputCapability> getProvidedOutputs() const override {
        return {opk::outputCapability<open_perception_kit::metadata::VoiceActivityDetectionsT>()};
    }
    opk::Result<void> parse(const opk::TensorParser::Input &input,
                            open_perception_kit::FrameResults &results) override;

  private:
    bool speechActive_ = false;
    uint64_t attackSamples_ = 0;
    uint64_t releaseSamples_ = 0;
};

} // namespace opk::stdop::postproc

#endif // OPK_SILERO_VAD_PARSER_H
