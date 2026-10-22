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

#ifndef OPK_GENERIC_AUDIO_PREPROCESS_OP_H
#define OPK_GENERIC_AUDIO_PREPROCESS_OP_H

#include "op/Op.h"

namespace opk::stdop {

/// Copies one scheduled mono Float32 16 kHz PCM window into the next inference Op.
/// Attributes: inputAudioSourceName (pipelineAudioData), inputAudioTensorIndex
/// (auto-detect a unique AudioPcm input when omitted or -1).
/// contextSampleCount prepends retained history, zero-initialized on discontinuities.
/// Supports [N] and singleton leading dimensions; no resampling or log-mel.
class GenericAudioPreprocessOp : public opk::op::Op, public opk::op::OpInterfaceAudioPreprocessor {
  public:
    opk::Result<void> configure(const opk::AttributeMap &attributes) override;
    opk::Result<void> bind(size_t index, const std::vector<opk::op::Op *> &ops) override;
    opk::Result<opk::op::OpSignal> process(opk::op::OpChainContext &context) override;

    size_t getRequiredAudioSamples() const override {
        return sampleCount - contextSamples.size();
    }

  private:
    size_t contextSampleCount = 0;
    std::vector<float> contextSamples;
    std::optional<uint64_t> nextPosition;
    std::string inputAudioSourceName = "pipelineAudioData";
    size_t inputAudioTensorIndex = opk::InvalidTensorIndex;
    const opk::op::OpInterfaceInference *inference = nullptr;
    size_t sampleCount = 0;
};

} // namespace opk::stdop

#endif // OPK_GENERIC_AUDIO_PREPROCESS_OP_H
