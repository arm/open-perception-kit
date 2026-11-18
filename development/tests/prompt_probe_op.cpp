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
#include "opk/BoxPromptInputs.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>

namespace {

// Test counters outlive the pipeline; this verifies scheduling, not model inference.
class ProbeInference : public opk::op::Op, public opk::op::OpInterfaceInference {
  public:
    virtual bool consumesPrompt() const {
        return false;
    }

    opk::Result<void> configure(const opk::AttributeMap &attributes) override {
        counter = reinterpret_cast<std::atomic<unsigned> *>(
            static_cast<std::uintptr_t>(attributes.getInt("counterAddress")));
        promptCounter = reinterpret_cast<std::atomic<unsigned> *>(
            static_cast<std::uintptr_t>(attributes.getIntOrDefault("promptCounterAddress", 0)));
        return {};
    }

    opk::Result<void> bind(size_t, const std::vector<opk::op::Op *> &) override {
        return {};
    }

    const opk::Model &getModel() const override {
        return model;
    }

    uint8_t *getTensorDataAddress(size_t) const override {
        return nullptr;
    }

    opk::Result<opk::op::OpSignal> process(opk::op::OpChainContext &context) override {
        counter->fetch_add(1);
        if (context.prompt && promptCounter) {
            promptCounter->fetch_add(1);
        }
        const auto *const frame = context.getVideoFrame("pipelineVideoFrame");
        if (!frame || !context.frameResults) {
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidData, "Missing probe video context"));
        }
        // Synthetic output exercises core request/media association without running a model.
        auto object = open_perception_kit::makeObjectMeta();
        context.inferenceInfo.parentId = object->id;
        const auto producer = producerInfo(
            context.inferenceInfo.inferElementId, "scheduling-probe", "opk-test-prompt-probe");
        open_perception_kit::metadata::FrameContextT media;
        media.video = std::make_unique<open_perception_kit::metadata::VideoFrameContextT>();
        media.video->object = std::move(object);
        media.video->original_width = frame->width();
        media.video->original_height = frame->height();
        context.frameResults->add(std::move(media));
        if (context.prompt && consumesPrompt() && context.prompt->payload->format().id == "TEST") {
            open_perception_kit::metadata::BoxDetectionsT detections;
            detections.layer = open_perception_kit::makeLayerInfo(
                {.model = "scheduling-probe",
                 .inferElementId = context.inferenceInfo.inferElementId,
                 .contentType = "detection",
                 .producer = &producer});
            opk::appendPromptAssociation(*context.frameResults,
                                         *context.prompt,
                                         *detections.layer,
                                         context.inferenceInfo.parentId);
            context.frameResults->add(std::move(detections));
            return opk::op::OpSignal::Continue;
        }
        open_perception_kit::metadata::InstanceSegmentationMasksT masks;
        masks.layer = open_perception_kit::makeLayerInfo({
            .model = "scheduling-probe",
            .inferElementId = context.inferenceInfo.inferElementId,
            .contentType = "instanceSegmentation",
            .producer = &producer,
        });
        if (context.prompt && consumesPrompt()) {
            opk::appendPromptAssociation(*context.frameResults,
                                         *context.prompt,
                                         *masks.layer,
                                         context.inferenceInfo.parentId);
        }
        context.frameResults->add(std::move(masks));
        return opk::op::OpSignal::Continue;
    }

  private:
    opk::Model model;
    std::atomic<unsigned> *counter = nullptr;
    std::atomic<unsigned> *promptCounter = nullptr;
};

class PromptProbeInference final : public ProbeInference,
                                   public opk::op::OpInterfacePromptConsumer {
  public:
    bool consumesPrompt() const override {
        return true;
    }

    bool acceptsPrompt(const opk::PromptFormat format,
                       const std::string_view inferenceInstance,
                       const std::string_view sourceName) const override {
        return (format == opk::BoxPromptFormat || format == opk::PromptFormat{"TEST", 1, 0}) &&
               inferenceInstance == instanceId && sourceName == "pipelineVideoFrame";
    }
};

} // namespace

extern "C" void *opk_create_op_instance(const char *const name) {
    if (name && std::strcmp(name, "PlainInference") == 0) {
        return new ProbeInference();
    }
    if (name && std::strcmp(name, "PromptInference") == 0) {
        return new PromptProbeInference();
    }
    return nullptr;
}

extern "C" void opk_delete_op_instance(void *const instance) {
    delete static_cast<opk::op::Op *>(instance);
}
