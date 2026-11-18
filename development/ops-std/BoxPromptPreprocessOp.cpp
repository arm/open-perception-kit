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

#include "BoxPromptPreprocessOp.h"

#include "GenericImagePreprocessOp.h"
#include "opk/BoxPromptInputs.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace opk::stdop {

opk::op::InferenceInputBinding BoxPromptPreprocessOp::getInputBinding() const {
    return {inferenceInstanceId, inputImageSourceName, {&inputBoxTensorIndex, 1}};
}

bool BoxPromptPreprocessOp::acceptsPrompt(const opk::PromptFormat format,
                                          const std::string_view inferenceInstance,
                                          const std::string_view sourceName) const {
    return format == opk::BoxPromptFormat && inference != nullptr &&
           inferenceInstanceId == inferenceInstance && inputImageSourceName == sourceName;
}

opk::Result<void> BoxPromptPreprocessOp::configure(const opk::AttributeMap &attributes) {
    for (const auto &[name, value] : attributes.raw()) {
        if (name != "inferenceInstance" && name != "inputBoxTensorIndex" &&
            name != "inputImageSourceName") {
            return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                            "Unknown BoxPromptPreprocess attribute: " + name));
        }
        if ((name == "inputBoxTensorIndex" && !value.isInt()) ||
            (name != "inputBoxTensorIndex" && !value.isString())) {
            return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                            "Invalid BoxPromptPreprocess attribute: " + name));
        }
    }
    if (!attributes.contains("inferenceInstance") || !attributes.contains("inputBoxTensorIndex")) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "BoxPromptPreprocess needs inferenceInstance and inputBoxTensorIndex"));
    }
    inferenceInstanceId = attributes.getString("inferenceInstance");
    inputImageSourceName =
        attributes.getStringOrDefault("inputImageSourceName", "pipelineVideoFrame");
    const auto index = attributes.getInt("inputBoxTensorIndex");
    if (index < 0 || index >= static_cast<int64_t>(opk::MaxTensorCount) ||
        inferenceInstanceId.empty() || inputImageSourceName.empty()) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "BoxPromptPreprocess requires a valid tensor index and destination"));
    }
    inputBoxTensorIndex = static_cast<size_t>(index);
    return {};
}

opk::Result<void> BoxPromptPreprocessOp::bind(const size_t index,
                                              const std::vector<opk::op::Op *> &ops) {
    if (index == 0 || index >= ops.size() || ops.size() - index < 2 || loopId != 0 ||
        ops[index - 1]->loopId != 0 || ops[index + 1]->loopId != 0) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "BoxPromptPreprocess requires an unlooped image/box/inference sequence"));
    }
    const auto *const image = ops[index - 1]->as<GenericImagePreprocessOp>();
    inference = ops[index + 1]->as<opk::op::OpInterfaceInference>();
    sharedInput = ops[index + 1]->as<opk::op::OpInterfaceSharedInput>();
    if (!image || !inference || ops[index + 1]->instanceId != inferenceInstanceId ||
        image->inferenceInstance() != inferenceInstanceId ||
        image->imageSourceName() != inputImageSourceName ||
        image->imageTensorIndex() == inputBoxTensorIndex) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                        "BoxPromptPreprocess needs matching explicit image binding "
                                        "and the configured source"));
    }
    const auto &model = inference->getModel();
    if (model.inputs.size() != 2 || inputBoxTensorIndex >= model.inputs.size() ||
        image->imageTensorIndex() >= model.inputs.size()) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "BoxPromptPreprocess requires distinct image and box inputs"));
    }
    const auto &boxInput = model.inputs[inputBoxTensorIndex];
    const auto &imageInput = model.inputs[image->imageTensorIndex()];
    if (boxInput.valueType != opk::Dtype::Float32 || !(boxInput.shape == opk::Shape(1, 1, 2, 2)) ||
        !boxInput.valueInputs.empty() ||
        boxInput.matchShapeOutputIndex != opk::InvalidTensorIndex ||
        imageInput.valueType != opk::Dtype::Float32 ||
        imageInput.dataKind != opk::DataKind::ImageRgbChw ||
        !(imageInput.shape == opk::Shape(1, 3, 1024, 1024)) || imageInput.keepAspectRatio ||
        !imageInput.valueInputs.empty() ||
        imageInput.matchShapeOutputIndex != opk::InvalidTensorIndex ||
        !opk::MeanStd::isDefaultMean(imageInput.mean) ||
        !opk::MeanStd::isDefaultStd(imageInput.std)) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                        "BoxPromptPreprocess requires the qualified Float32 "
                                        "full-image stretch and box contract"));
    }
    return {};
}

opk::Result<opk::op::OpSignal> BoxPromptPreprocessOp::process(opk::op::OpChainContext &context) {
    context.preparedPrompt.reset();
    if (!context.prompt) {
        return opk::op::OpSignal::AbortChain;
    }
    const auto &prompt = *context.prompt;
    const auto validation =
        opk::validateBoxPrompt(prompt, inferenceInstanceId, inputImageSourceName);
    if (!validation) {
        return tl::unexpected(validation.error());
    }
    if (!prompt.payload) {
        return opk::op::OpSignal::AbortChain;
    }
    const auto &box = static_cast<const opk::BoxPromptPayload &>(*prompt.payload).box;
    const auto *const frame = context.getVideoFrame(inputImageSourceName);
    const auto &image = context.inferenceInfo.image;
    bool fullFrame = false;
    if (context.frameResults) {
        context.frameResults->for_each<open_perception_kit::metadata::FrameContextT>(
            [&](const auto &frameContext) {
                if (frameContext.video && frameContext.video->object &&
                    frameContext.video->object->id == context.inferenceInfo.parentId) {
                    const auto &video = *frameContext.video;
                    fullFrame = video.original_width == image.width &&
                                video.original_height == image.height && image.cropX == 0 &&
                                image.cropY == 0 && image.frameId == video.object->id &&
                                image.frameWidth == video.original_width &&
                                image.frameHeight == video.original_height;
                }
            });
    }
    if (!inference || !frame || !fullFrame || context.inferenceInfo.parentId == 0 ||
        image.width == 0 || image.height == 0 || image.width != frame->width() ||
        image.height != frame->height() || image.modelWidth != 1024 || image.modelHeight != 1024 ||
        image.letterboxLeft != 0 || image.letterboxRight != 0 || image.letterboxTop != 0 ||
        image.letterboxBottom != 0) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "BoxPromptPreprocess requires the current uncropped full-image stretch"));
    }
    const std::array<float, 4> corners{
        static_cast<float>(box.left * static_cast<double>(image.modelWidth)),
        static_cast<float>(box.top * static_cast<double>(image.modelHeight)),
        static_cast<float>(box.right * static_cast<double>(image.modelWidth)),
        static_cast<float>(box.bottom * static_cast<double>(image.modelHeight))};
    if (corners[0] >= corners[2] || corners[1] >= corners[3]) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "BoxPromptPreprocess box collapses in Float32 model coordinates"));
    }
    if (sharedInput) {
        const auto owned = sharedInput->setOwnedInputStorage(inputBoxTensorIndex);
        if (!owned) {
            return tl::unexpected(owned.error());
        }
    }
    auto *const destination = inference->getTensorDataAddress(inputBoxTensorIndex);
    if (!destination) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "BoxPromptPreprocess box input is not writable"));
    }
    std::memcpy(destination, corners.data(), sizeof(corners));
    context.preparedPrompt = context.prompt;
    return opk::op::OpSignal::Continue;
}

} // namespace opk::stdop
