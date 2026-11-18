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

#include "GenericImagePreprocessOp.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fmt/core.h>
#include <memory>
#include <string>

#include "Log.h"
#include "mediaio/Common.h"
#include "opk/ImageOpDesc.h"
#include "opk/Result.h"
#include "opk/TensorView.h"
#include "opk/Types.h"
#include "tl/expected.hpp"

#include <perf/PerformanceMetrics.h>

using namespace opk::stdop;

namespace {

bool isYuvPixelFormat(opk::RawImagePixelFormat format) noexcept {
    using enum opk::RawImagePixelFormat;

    switch (format) {
    case I420:
    case Nv12:
    case Yuy2:
        return true;
    default:
        return false;
    }
}

opk::ImagePlaneDesc makePlaneDesc(const opk::mediaio::DataView &plane) {
    return {
        static_cast<const uint8_t *>(plane.data()),
        nullptr,
        plane.byteSize(),
        plane.strideBytes(),
    };
}

opk::Result<opk::ImageOpDesc> sourceImageDescription(const opk::mediaio::VideoFrame &frame,
                                                     const opk::PixelRect &cropRect,
                                                     const opk::ModelInput &inputTensor) {
    const auto readablePlanes{frame.planes()};

    const auto sourceFormat = frame.format();
    const size_t sourcePlaneCount = readablePlanes.size();
    if (sourcePlaneCount == 0 || sourcePlaneCount > opk::MaxImagePlaneCount) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "GenericImagePreprocessOp VideoFrame has invalid plane count"));
    }

    // setup tensor data source
    opk::ImageOpDesc source{};
    source.surfaceWidth = frame.width();
    source.surfaceHeight = frame.height();
    source.rect = cropRect;
    source.planeCount = sourcePlaneCount;
    for (size_t planeIndex = 0; planeIndex < sourcePlaneCount; ++planeIndex) {
        source.planes[planeIndex] = makePlaneDesc(readablePlanes[planeIndex]);
        if (!source.planes[planeIndex].data || source.planes[planeIndex].mutableData) {
            opk::log::error("GenericImagePreprocessOp source plane {} is not readable\n",
                            planeIndex);
            return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                            "GenericImagePreprocessOp source plane " +
                                                std::to_string(planeIndex) + " is not readable"));
        }
    }
    source.format = sourceFormat;
    source.type = opk::Dtype::Uint8;
    source.mean = inputTensor.mean;
    source.std = inputTensor.std;
    if (isYuvPixelFormat(sourceFormat)) {
        source.yuvMatrix = frame.yuvColorMatrix();
        source.yuvRange = frame.yuvRange();
        if (source.yuvMatrix == opk::YuvColorMatrix::Unknown ||
            source.yuvRange == opk::YuvRange::Unknown) {
            return tl::unexpected(OPK_ERROR(
                opk::ErrorFlag::InvalidData,
                "GenericImagePreprocessOp YUV VideoFrame has unknown colorimetry or range"));
        }
    }

    return source;
}

opk::Result<void> validateInputPreprocessors(const std::span<opk::op::Op *const> operations,
                                             const opk::op::InferenceInputBinding &imageBinding,
                                             const size_t inputCount) {
    std::array<bool, opk::MaxTensorCount> written{};
    const auto claimInputs =
        [&](const opk::op::InferenceInputBinding &binding) -> opk::Result<void> {
        if (binding.inferenceInstance != imageBinding.inferenceInstance ||
            binding.sourceName != imageBinding.sourceName || binding.tensorIndices.empty()) {
            return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                            "Input preprocessors require matching inference/source "
                                            "bindings and declared inputs"));
        }
        for (const size_t tensorIndex : binding.tensorIndices) {
            if (tensorIndex >= inputCount || tensorIndex >= written.size() ||
                written[tensorIndex]) {
                return tl::unexpected(
                    OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                              "Input preprocessors require valid, nonoverlapping tensor inputs"));
            }
            written[tensorIndex] = true;
        }
        return {};
    };

    if (const auto result = claimInputs(imageBinding); !result) {
        return result;
    }

    for (const auto *const operation : operations) {
        const auto *const preprocessor =
            operation->as<opk::op::OpInterfaceInferenceInputPreprocessor>();
        if (operation->loopId != 0 || operation->as<opk::op::OpInterfaceInference>() ||
            !preprocessor) {
            return tl::unexpected(OPK_ERROR(
                opk::ErrorFlag::InvalidOpChain,
                "Explicit image binding permits only unlooped cooperating input preprocessors "
                "before inference"));
        }
        const auto binding = preprocessor->getInputBinding();
        if (const auto result = claimInputs(binding); !result) {
            return result;
        }
    }
    return {};
}

opk::ImageInferenceMetadata imageMetadata(const opk::PixelRect &cropRect,
                                          const opk::PixelRect &destinationRect,
                                          const opk::PixelRect &letterboxInnerRect) {
    opk::ImageInferenceMetadata image{};
    image.width = cropRect.width;
    image.height = cropRect.height;
    image.modelWidth = destinationRect.width;
    image.modelHeight = destinationRect.height;
    image.letterboxLeft = letterboxInnerRect.x - destinationRect.x;
    image.letterboxRight = destinationRect.width - letterboxInnerRect.width - image.letterboxLeft;
    image.letterboxTop = letterboxInnerRect.y - destinationRect.y;
    image.letterboxBottom = destinationRect.height - letterboxInnerRect.height - image.letterboxTop;
    return image;
}

} // namespace

GenericImagePreprocessOp::GenericImagePreprocessOp() = default;
GenericImagePreprocessOp::~GenericImagePreprocessOp() = default;

opk::Result<void> GenericImagePreprocessOp::bind(const size_t index,
                                                 const std::vector<opk::op::Op *> &ops) {
    // sanity check
    if (index >= ops.size() || ops.size() - index <= 1) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                        "GenericImagePreprocessOp cannot be the last Op"));
    }

    size_t inferenceIndex = index + 1;
    if (!inferenceInstanceId.empty()) {
        const auto destination = std::find_if(
            ops.begin() + static_cast<std::ptrdiff_t>(index + 1),
            ops.end(),
            [&](const auto *const op) { return op->instanceId == inferenceInstanceId; });
        if (destination == ops.end() || loopId != 0 || (*destination)->loopId != 0) {
            return tl::unexpected(OPK_ERROR(
                opk::ErrorFlag::InvalidOpChain,
                "GenericImagePreprocess explicit inference must follow it in an unlooped chain"));
        }
        inferenceIndex = static_cast<size_t>(destination - ops.begin());
    }
    upcomingInference = ops[inferenceIndex]->as<opk::op::OpInterfaceInference>();
    if (!upcomingInference) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "GenericImagePreprocessOp should be BEFORE an inference Op"));
    }

    // --- grab infos about the upcoming inference
    upcomingInferenceModel = upcomingInference->getModel();
    sharedInput = ops[inferenceIndex]->as<opk::op::OpInterfaceSharedInput>();

    if (inputImageTensorIndex == opk::InvalidTensorIndex) {
        // user not specified a tensor index
        // we try to discover here which upcoming tensor has image-like shape
        for (size_t i = 0; i < upcomingInferenceModel.inputs.size(); i++) {
            size_t w, h;
            if (upcomingInferenceModel.inputs[i].tryGetImageTensorSize(w, h)) {
                inputImageTensorIndex = i;
                break;
            }
        }
    }

    if (inputImageTensorIndex >= upcomingInferenceModel.inputs.size()) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "GenericImagePreprocess image tensor index is out of range"));
    }

    // sanity check
    if (inputImageTensorIndex == opk::InvalidTensorIndex) {
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidOpChain,
            "GenericImagePreprocessOp cannot find an upcoming tensor index to work with"));
    }

    if (!inferenceInstanceId.empty()) {
        const opk::op::InferenceInputBinding imageBinding{
            inferenceInstanceId, inputImageSourceName, {&inputImageTensorIndex, 1}};
        const std::span<opk::op::Op *const> intermediates{ops.data() + index + 1,
                                                          inferenceIndex - index - 1};
        return validateInputPreprocessors(
            intermediates, imageBinding, upcomingInferenceModel.inputs.size());
    }
    return {};
}

opk::Result<void> GenericImagePreprocessOp::configure(const opk::AttributeMap &attributes) {
    for (const auto *const name : {"inputImageSourceName", "inferenceInstance"}) {
        if (attributes.contains(name) && !attributes.at(name).isString()) {
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidData, std::string(name) + " must be a string"));
        }
    }
    if (attributes.contains("inputImageTensorIndex") &&
        (!attributes.at("inputImageTensorIndex").isInt() ||
         attributes.getInt("inputImageTensorIndex") < 0 ||
         attributes.getInt("inputImageTensorIndex") >= static_cast<int64_t>(opk::MaxTensorCount))) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData, "inputImageTensorIndex must be in [0,15]"));
    }
    // --- get config info from the json attributes
    inputImageSourceName =
        attributes.getStringOrDefault("inputImageSourceName", "pipelineVideoFrame");
    inputImageTensorIndex =
        attributes.getIntOrDefault("inputImageTensorIndex", opk::InvalidTensorIndex);
    inferenceInstanceId = attributes.getStringOrDefault("inferenceInstance", "");
    if (inputImageSourceName.empty() ||
        (attributes.contains("inferenceInstance") && inferenceInstanceId.empty())) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "Image source and explicit destination must be nonempty"));
    }
    return {};
}

const std::string &GenericImagePreprocessOp::inferenceInstance() const {
    return inferenceInstanceId;
}

size_t GenericImagePreprocessOp::imageTensorIndex() const {
    return inputImageTensorIndex;
}

const std::string &GenericImagePreprocessOp::imageSourceName() const {
    return inputImageSourceName;
}

opk::Result<opk::ImageOpDesc>
GenericImagePreprocessOp::destinationImageDescription(const opk::ModelInput &inputTensor,
                                                      const opk::PixelRect &destinationRect) {
    if (sharedInput) {
        const auto result{sharedInput->setOwnedInputStorage(inputImageTensorIndex)};
        if (!result) {
            return tl::unexpected(result.error());
        }
    }
    uint8_t *const inputTensorData{upcomingInference->getTensorDataAddress(inputImageTensorIndex)};

    opk::ImageOpDesc destination{};
    destination.type = inputTensor.valueType;
    destination.surfaceWidth = destinationRect.width;
    destination.surfaceHeight = destinationRect.height;
    destination.rect = destinationRect;
    destination.kind = inputTensor.dataKind;
    destination.planeCount = 1;
    destination.planes[0] = {
        nullptr,
        inputTensorData,
        inputTensor.shape.getFullValueCount() * opk::getValueTypeByteSize(inputTensor.valueType),
        0,
    };
    if (destination.planes[0].data || !destination.planes[0].mutableData) {
        opk::log::error("GenericImagePreprocessOp input tensor {} is not writable\n",
                        inputImageTensorIndex);
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "GenericImagePreprocessOp input tensor " +
                                            std::to_string(inputImageTensorIndex) +
                                            " is not writable"));
    }
    destination.keepAspectRatio = inputTensor.keepAspectRatio;
    destination.letterboxRed = inputTensor.letterboxRed;
    destination.letterboxGreen = inputTensor.letterboxGreen;
    destination.letterboxBlue = inputTensor.letterboxBlue;

    return destination;
}

opk::Result<opk::op::OpSignal>
GenericImagePreprocessOp::process(opk::op::OpChainContext &opChainContext) {
    OPK_PERF_SCOPE(fmt::format("std/GenImgPre/{}", upcomingInferenceModel.name));

    if (opChainContext.inferenceImageCrops.size() == 0) {
        return opk::op::OpSignal::BreakLoop;
    }
    if (opChainContext.inferenceImageCropIds.size() != opChainContext.inferenceImageCrops.size()) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "GenericImagePreprocess crop IDs are inconsistent"));
    }

    const opk::PixelRect cropRect{opChainContext.inferenceImageCrops.back()};
    opChainContext.inferenceImageCrops.pop_back();
    const uint64_t sourceId{opChainContext.inferenceImageCropIds.back()};
    opChainContext.inferenceImageCropIds.pop_back();

    const auto *const pipelineVideoFrame{opChainContext.getVideoFrame(inputImageSourceName)};

    if (pipelineVideoFrame == nullptr) {
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidOpChain,
            fmt::format("GenericImagePreprocessOp needs VideoFrame '{}'", inputImageSourceName)));
    }

    size_t modelWidth{0}, modelHeight{0};
    const auto &inputTensor = upcomingInferenceModel.inputs[inputImageTensorIndex];
    if (false == inputTensor.tryGetImageTensorSize(modelWidth, modelHeight)) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData, "tensor seems not to be an image"));
    }

    const auto *const readableVideoFrame{
        opChainContext.getReadableVideoFrame(inputImageSourceName)};
    if (!readableVideoFrame) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "GenericImagePreprocessOp failed to map readable host planes"));
    }
    const auto source{sourceImageDescription(*readableVideoFrame, cropRect, inputTensor)};
    if (!source)
        return tl::unexpected(source.error());
    const opk::PixelRect destinationRect{0, 0, modelWidth, modelHeight};
    const auto destination{destinationImageDescription(inputTensor, destinationRect)};
    if (!destination)
        return tl::unexpected(destination.error());
    const opk::TensorBuilder::Setup setup{*source, *destination};

    opk::PixelRect letterboxInnerRect = setup.imageDestinationDesc.rect;
    if (inputTensor.keepAspectRatio) {
        letterboxInnerRect =
            opk::computeLetterboxInnerRect(cropRect, setup.imageDestinationDesc.rect);
    }

    // call tensor building
    const auto result{genericImageInputTensorBuilder.build(setup)};
    if (result.has_value() == false) {
        return tl::unexpected(result.error());
    }

    opChainContext.inferenceInfo.image =
        imageMetadata(cropRect, destinationRect, letterboxInnerRect);
    auto &image = opChainContext.inferenceInfo.image;
    const auto producer = producerInfo(
        opChainContext.inferenceInfo.inferElementId, opName, "opk-std-ops/GenericImagePreprocess");
    image.frameId = opChainContext.videoFrameId(inputImageSourceName, producer);
    image.frameWidth = pipelineVideoFrame->width();
    image.frameHeight = pipelineVideoFrame->height();
    image.cropX = cropRect.x;
    image.cropY = cropRect.y;
    opChainContext.inferenceInfo.modelName = upcomingInferenceModel.name;
    opChainContext.inferenceInfo.parentId = sourceId;

    return opk::op::OpSignal::Continue;
}
