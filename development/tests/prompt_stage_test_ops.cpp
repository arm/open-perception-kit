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

#include "prompt_stage_test_state.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace {

class StageOp : public opk::op::Op {
  public:
    enum class Kind { Prepare, Crop, Observe };

    explicit StageOp(const Kind kind) : kind(kind) {}

    opk::Result<void> configure(const opk::AttributeMap &attributes) override {
        state = reinterpret_cast<PromptStageTestState *>(
            static_cast<std::uintptr_t>(attributes.getInt("stateAddress")));
        if (!state) {
            return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData, "Missing test state"));
        }
        return {};
    }

    opk::Result<void> bind(const size_t, const std::vector<opk::op::Op *> &) override {
        return {};
    }

    opk::Result<opk::op::OpSignal> process(opk::op::OpChainContext &context) override {
        ++state->calls;
        switch (kind) {
        case Kind::Prepare:
            if (state->preparePrompt) {
                context.preparedPrompt = context.prompt;
            }
            break;
        case Kind::Crop:
            context.inferenceImageCrops = {{0, 0, 2, 2}};
            context.inferenceImageCropIds = {42};
            break;
        case Kind::Observe:
            state->observed.push_back(context.inferencePrompt);
            state->sawPreparedAfterInference |= context.preparedPrompt != nullptr;
            break;
        }
        return result();
    }

  protected:
    opk::Result<opk::op::OpSignal> result() const {
        if (state->error) {
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidData, "Synthetic stage failure"));
        }
        if (state->breakAfter != 0 && state->calls >= state->breakAfter) {
            return opk::op::OpSignal::BreakLoop;
        }
        return state->signal;
    }

    PromptStageTestState *state = nullptr;

  private:
    const Kind kind;
};

// Only the inference is synthetic; the regression can use the real OCR preprocessor.
class StageInference final : public StageOp, public opk::op::OpInterfaceInference {
  public:
    StageInference() : StageOp(Kind::Observe) {
        opk::ModelInput input;
        input.valueType = opk::Dtype::Float32;
        input.dataKind = opk::DataKind::ImageBgrChw;
        input.shape = opk::Shape(1, 3, 2, 2);
        model.inputs.push_back(input);
    }

    const opk::Model &getModel() const override {
        return model;
    }
    uint8_t *getTensorDataAddress(const size_t index) const override {
        return index == 0 ? reinterpret_cast<uint8_t *>(input.data()) : nullptr;
    }

    opk::Result<opk::op::OpSignal> process(opk::op::OpChainContext &context) override {
        ++state->calls;
        state->sawAssociationDuringInference |= context.inferencePrompt != nullptr;
        if (state->produceOutput) {
            context.inferenceOutputTensorCount = 1;
            context.inferenceOutputTensors[0] =
                opk::TensorView(state->validOutput ? &output : nullptr,
                                sizeof(output),
                                opk::Shape(1),
                                opk::Dtype::Float32,
                                1,
                                0);
        }
        return result();
    }

  private:
    opk::Model model;
    mutable std::array<float, 12> input{};
    float output = 0.75F;
};

} // namespace

extern "C" void *opk_create_op_instance(const char *const name) {
    if (!name) {
        return nullptr;
    }
    if (std::strcmp(name, "Prepare") == 0) {
        return new StageOp(StageOp::Kind::Prepare);
    }
    if (std::strcmp(name, "Crop") == 0) {
        return new StageOp(StageOp::Kind::Crop);
    }
    if (std::strcmp(name, "Observe") == 0) {
        return new StageOp(StageOp::Kind::Observe);
    }
    if (std::strcmp(name, "Inference") == 0) {
        return new StageInference();
    }
    return nullptr;
}

extern "C" void opk_delete_op_instance(void *const instance) {
    delete static_cast<opk::op::Op *>(instance);
}
