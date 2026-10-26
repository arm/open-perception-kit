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

#include "op/Op.h"

namespace opk::stdop {

/// @brief Gates turn inference using VAD results and retained mono Float32 16 kHz PCM.
///
/// Must immediately follow a VAD postprocessor and precede a PCM inference Op in an
/// unlooped chain. Each call consumes one fresh, nonoverlapping VAD hop. After speech
/// followed by the configured silence, it prepares a model-sized history window,
/// left-padded with zeros when necessary. It performs no resampling or log-mel extraction.
///
/// Retries are bounded per pause, and new speech rearms them. Initial silence never
/// triggers inference. Discontinuities or gaps reset history and gating state.
/// Skipping an evaluation aborts the remaining chain while preserving existing VAD results.
class TurnDetectorOp : public opk::op::Op, public opk::op::OpInterfaceAudioPreprocessor {
  public:
    /// @brief Validates configuration and clears the previous binding and retained history.
    /// @param attributes Supports hopSampleCount (512), silenceMs (300), retryMs (500),
    ///        maxAttempts (3), inputAudioSourceName (pipelineAudioData), and vadModel
    ///        (silero-vad). Durations are converted to sample counts at 16 kHz.
    /// @return Success, or InvalidOpChain for invalid attribute values.
    opk::Result<void> configure(const opk::AttributeMap &attributes) override;

    /// @brief Validates adjacent Ops and allocates history for the next model's PCM input.
    /// @param index This Op's position in @p ops.
    /// @param ops Chain Ops; the predecessor must provide voiceActivity and the successor
    ///        must expose one fixed Float32 AudioPcm input with shape [1,N].
    /// @return Success, or InvalidOpChain for an unsupported layout, loop, or input window.
    opk::Result<void> bind(size_t index, const std::vector<opk::op::Op *> &ops) override;

    /// @brief Retains the current hop and prepares turn inference when the gate opens.
    /// @param[in,out] context PCM hop, audio timing, and accumulated FrameResults. Exactly
    ///        one VAD result must match the hop bounds, configured model, and infer element.
    ///        On Continue, model identity and audio bounds describe the prepared history;
    ///        synthetic padding is excluded from sample bounds and timestamps.
    /// @return Continue when input is ready, AbortChain when evaluation is skipped, or
    ///         InvalidData when binding, input, VAD results, or history preparation is invalid.
    opk::Result<opk::op::OpSignal> process(opk::op::OpChainContext &context) override;

    /// @brief Returns the fresh PCM hop required by the audio scheduler.
    /// @return Samples per call, rather than the longer turn-model input window.
    size_t getRequiredAudioSamples() const override {
        return hopSampleCount;
    }

  private:
    /// @brief Clears history and gating state and marks the next assessment discontinuous.
    /// @pre History has been allocated by bind().
    /// @param nextPosition Absolute sample position at which retained history restarts.
    void reset(uint64_t nextPosition);

    /// Number of fresh 16 kHz samples consumed per process() call.
    size_t hopSampleCount = 512;
    /// Minimum VAD-inactive samples after the latest voiced hop before an assessment.
    uint64_t silenceSamples = 4800;
    /// Minimum samples between assessments during the same pause.
    uint64_t retrySamples = 8000;
    /// Maximum assessments per pause, including the first attempt.
    size_t maxAttempts = 3;
    /// Name of the scheduled PCM view in OpChainContext.
    std::string inputAudioSourceName = "pipelineAudioData";
    /// Model identity required on the matching VAD payload.
    std::string vadModel = "silero-vad";
    /// Non-owning pointer to the adjacent inference Op, valid while the chain is bound.
    const opk::op::OpInterfaceInference *inference = nullptr;
    /// Real PCM history, with capacity equal to the bound model's input sample count.
    std::unique_ptr<opk::PcmRingBuffer> history;
    /// Contiguous model input scratch space, including leading zero padding.
    std::vector<float> paddedWindow;
    /// Expected start of the next hop; a mismatch indicates a gap or overlap.
    std::optional<uint64_t> nextPosition;
    /// Exclusive end of the latest voiced hop since the last reset.
    std::optional<uint64_t> lastSpeechEnd;
    /// Exclusive window end of the latest assessment in the current pause.
    std::optional<uint64_t> lastAttemptEnd;
    /// Assessments prepared since the latest voiced hop or reset.
    size_t attempts = 0;
    /// Carries a reset marker through skipped hops until the next prepared assessment.
    bool pendingDiscontinuity = true;
};

} // namespace opk::stdop
