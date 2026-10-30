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

opk::Result<void> GenericImagePreprocessOp::bind(size_t index,
                                                 const std::vector<opk::op::Op *> &ops) {
    // sanity check
    if (index >= ops.size() || ops.size() - index <= 1) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                        "GenericImagePreprocessOp cannot be the last Op"));
    }

    // sanity check (later maybe other ops will be valid between them)
    upcomingInference = ops[index + 1]->as<opk::op::OpInterfaceInference>();
    if (!upcomingInference) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "GenericImagePreprocessOp should be BEFORE an inference Op"));
    }

    // --- grab infos about the upcoming inference
    upcomingInferenceModel = upcomingInference->getModel();
    sharedInput = ops[index + 1]->as<opk::op::OpInterfaceSharedInput>();

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

    // sanity check
    if (inputImageTensorIndex == opk::InvalidTensorIndex) {
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidOpChain,
            "GenericImagePreprocessOp cannot find an upcoming tensor index to work with"));
    }

    return {};
}

opk::Result<void> GenericImagePreprocessOp::configure(const opk::AttributeMap &attributes) {
    // --- get config info from the json attributes
    inputImageSourceName =
        attributes.getStringOrDefault("inputImageSourceName", "pipelineVideoFrame");
    inputImageTensorIndex =
        attributes.getIntOrDefault("inputImageTensorIndex", opk::InvalidTensorIndex);
    return {};
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
    opChainContext.inferenceInfo.modelName = upcomingInferenceModel.name;
    opChainContext.inferenceInfo.contentType = upcomingInferenceModel.contentType;
    opChainContext.inferenceInfo.parentId = sourceId;

    return opk::op::OpSignal::Continue;
}
