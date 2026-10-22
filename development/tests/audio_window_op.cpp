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

#include "op/Op.h"
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>

namespace {
// A model-shaped test Op: verifies window delivery without a model or inference backend.
class AudioWindowOp final : public opk::op::Op, public opk::op::OpInterfaceInference {
    opk::Model model_;
    std::string trace_;

  public:
    opk::Result<void> configure(const opk::AttributeMap &attributes) override {
        trace_ = attributes.getString("trace");
        opk::ModelInput input;
        input.dataKind = attributes.getBoolOrDefault("logmel", false) ? opk::DataKind::AudioLogMel
                                                                      : opk::DataKind::AudioPcm;
        const auto window = attributes.getIntOrDefault("window", 512);
        const auto batch = attributes.getIntOrDefault("batch", 1);
        if (window < std::numeric_limits<int>::min() || window > std::numeric_limits<int>::max() ||
            batch < std::numeric_limits<int>::min() || batch > std::numeric_limits<int>::max())
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidData, "Audio test dimensions exceed int range"));
        input.shape.rank = 2;
        input.shape.dims[0] = static_cast<int>(batch);
        input.shape.dims[1] = static_cast<int>(window);
        model_.inputs.push_back(input);
        return {};
    }
    opk::Result<void> bind(size_t, const std::vector<opk::op::Op *> &) override {
        return {};
    }
    const opk::Model &getModel() const override {
        return model_;
    }
    uint8_t *getTensorDataAddress(size_t) const override {
        return nullptr;
    }
    opk::Result<opk::op::OpSignal> process(opk::op::OpChainContext &context) override {
        const auto *window = context.getPcmView("pipelineAudioData");
        if (!window || !context.frameResults)
            return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData, "Missing audio context"));
        std::vector<float> samples(window->size());
        if (!window->copyTo(samples))
            return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData, "Invalid window"));
        std::ofstream trace(trace_, std::ios::app);
        trace << window->beginPosition() << ' ' << window->size() << ' ' << samples.front() << ' '
              << samples.back() << ' ' << context.inferenceInfo.audio.discontinuity << ' '
              << context.inferenceInfo.audio.hopSampleCount << ' '
              << (context.inferenceInfo.audio.windowStartTimestampNs
                      ? std::to_string(*context.inferenceInfo.audio.windowStartTimestampNs)
                      : "none")
              << ' '
              << (context.inferenceInfo.audio.windowEndTimestampNs
                      ? std::to_string(*context.inferenceInfo.audio.windowEndTimestampNs)
                      : "none")
              << '\n';
        if (!trace)
            return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData, "Cannot write trace"));
        return opk::op::OpSignal::Continue;
    }
};
} // namespace
extern "C" void *opk_create_op_instance(const char *name) {
    if (!name || std::strcmp(name, "Window") != 0)
        return nullptr;
    return std::make_unique<AudioWindowOp>().release();
}
extern "C" void opk_delete_op_instance(void *instance) {
    std::unique_ptr<opk::op::Op> owner(static_cast<opk::op::Op *>(instance));
}
