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
#include <cstdlib>
#include <filesystem>
#include <fmt/core.h>
#include <memory>
#include <string>

#include "Log.h"
#include "mediaio/Common.h"
#include "opk/ImageOpDesc.h"
#include "opk/Result.h"
#include "opk/TensorView.h"
#include "opk/Tools.h"
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

std::filesystem::path debugOutputDirectory() {
    const char *projectRoot = std::getenv("OPK_PROJECT_ROOT");
    const std::filesystem::path root =
        projectRoot != nullptr && projectRoot[0] != '\0' ? projectRoot : "/work";
    return root / "var";
}

} // namespace

GenericImagePreprocessOp::GenericImagePreprocessOp() = default;
GenericImagePreprocessOp::~GenericImagePreprocessOp() = default;

opk::Result<void> GenericImagePreprocessOp::bind(size_t index,
                                                 const std::vector<opk::op::Op *> &ops) {
    // sanity check
    if (ops.size() <= index) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                        "GenericImagePreprocessOp cannot be the last Op"));
    }

    // sanity check (later maybe other ops will be valid between them)
    const opk::op::OpInterfaceInference *inferenceOp =
        ops[index + 1]->as<opk::op::OpInterfaceInference>();
    if (!inferenceOp) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "GenericImagePreprocessOp should be BEFORE an inference Op"));
    }

    // --- grab infos about the upcoming inference
    upcomingInferenceModel = inferenceOp->getModel();
    for (size_t i = 0; i < upcomingInferenceModel.inputs.size(); i++) {
        upcomingTensorAddresses[i] = inferenceOp->getTensorDataAddress(i);
    }

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

opk::Result<opk::op::OpSignal> GenericImagePreprocessOp::process(
    opk::op::OpChainContext &opChainContext) { // NOSONAR - preprocessing setup is intentionally
                                               // linear to keep frame/tensor state explicit.
    OPK_PERF_SCOPE(fmt::format("std/GenImgPre/{}", upcomingInferenceModel.name));

    if (opChainContext.inferenceImageCrops.size() == 0) {
        return opk::op::OpSignal::BreakLoop;
    }

    opk::PixelRect cropRect = opChainContext.inferenceImageCrops.back();
    opChainContext.inferenceImageCrops.pop_back();
    uint64_t sourceId = opChainContext.inferenceImageCropIds.back();
    opChainContext.inferenceImageCropIds.pop_back();

    auto *pipelineVideoFrame = opChainContext.getVideoFrame(inputImageSourceName);

    if (pipelineVideoFrame == nullptr) {
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidOpChain,
            fmt::format("GenericImagePreprocessOp needs VideoFrame '{}'", inputImageSourceName)));
    }

    size_t modelWidth, modelHeight;
    const auto &inputTensor = upcomingInferenceModel.inputs[inputImageTensorIndex];
    if (false == inputTensor.tryGetImageTensorSize(modelWidth, modelHeight)) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData, "tensor seems not to be an image"));
    }

    const opk::mediaio::VideoFrame *readableVideoFrame = pipelineVideoFrame;
    std::unique_ptr<opk::mediaio::VideoFrame> mappedPipelineVideoFrame;
    auto readablePlanes = readableVideoFrame->planes();
    auto hasReadableHostPlanes = [](auto planes) {
        if (planes.empty()) {
            return false;
        }
        return std::ranges::all_of(
            planes, [](const auto &plane) { return plane.hasHostData() && plane.canRead(); });
    };
    if (!hasReadableHostPlanes(readablePlanes)) {
        mappedPipelineVideoFrame = pipelineVideoFrame->map(opk::AccessMode::Read);
        if (!mappedPipelineVideoFrame) {
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidData,
                          "GenericImagePreprocessOp failed to map pipelineVideoFrame VideoFrame"));
        }

        readableVideoFrame = mappedPipelineVideoFrame.get();
        readablePlanes = readableVideoFrame->planes();
        if (!hasReadableHostPlanes(readablePlanes)) {
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidData,
                          "GenericImagePreprocessOp VideoFrame has no readable host planes"));
        }
    }

    const auto sourceFormat = readableVideoFrame->format();
    const size_t sourcePlaneCount = readablePlanes.size();
    if (sourcePlaneCount == 0 || sourcePlaneCount > opk::MaxImagePlaneCount) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "GenericImagePreprocessOp VideoFrame has invalid plane count"));
    }

    // setup tensor data source
    opk::TensorBuilder::Setup setup;
    setup.imageSourceDesc.surfaceWidth = readableVideoFrame->width();
    setup.imageSourceDesc.surfaceHeight = readableVideoFrame->height();
    setup.imageSourceDesc.rect = cropRect;
    setup.imageSourceDesc.planeCount = sourcePlaneCount;
    for (size_t planeIndex = 0; planeIndex < sourcePlaneCount; ++planeIndex) {
        setup.imageSourceDesc.planes[planeIndex] = makePlaneDesc(readablePlanes[planeIndex]);
        if (!setup.imageSourceDesc.planes[planeIndex].data ||
            setup.imageSourceDesc.planes[planeIndex].mutableData) {
            opk::log::error("GenericImagePreprocessOp source plane {} is not readable\n",
                            planeIndex);
            return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                            "GenericImagePreprocessOp source plane " +
                                                std::to_string(planeIndex) + " is not readable"));
        }
    }
    setup.imageSourceDesc.format = sourceFormat;
    setup.imageSourceDesc.type = opk::Dtype::Uint8;
    setup.imageSourceDesc.mean = inputTensor.mean;
    setup.imageSourceDesc.std = inputTensor.std;
    if (isYuvPixelFormat(sourceFormat)) {
        setup.imageSourceDesc.yuvMatrix = readableVideoFrame->yuvColorMatrix();
        setup.imageSourceDesc.yuvRange = readableVideoFrame->yuvRange();
        if (setup.imageSourceDesc.yuvMatrix == opk::YuvColorMatrix::Unknown ||
            setup.imageSourceDesc.yuvRange == opk::YuvRange::Unknown) {
            return tl::unexpected(OPK_ERROR(
                opk::ErrorFlag::InvalidData,
                "GenericImagePreprocessOp YUV VideoFrame has unknown colorimetry or range"));
        }
    }

    // debug
    if (false) {
        opk::log::info("crop: {} {} {} {}\n",
                       setup.imageSourceDesc.rect.x,
                       setup.imageSourceDesc.rect.y,
                       setup.imageSourceDesc.rect.width,
                       setup.imageSourceDesc.rect.height);
    }

    // debug
    if (false) {
        std::string debugFile =
            (debugOutputDirectory() / fmt::format("crop_[{}]_{}_{}x{}x{}x{}.png",
                                                  upcomingInferenceModel.contentType,
                                                  opk::nextObjectId(),
                                                  setup.imageSourceDesc.rect.x,
                                                  setup.imageSourceDesc.rect.y,
                                                  setup.imageSourceDesc.rect.width,
                                                  setup.imageSourceDesc.rect.height))
                .string();
        opk::Tools::savePngCropFromBgra(debugFile,
                                        setup.imageSourceDesc.planes[0].data,
                                        setup.imageSourceDesc.surfaceWidth,
                                        setup.imageSourceDesc.surfaceHeight,
                                        setup.imageSourceDesc.rect.x,
                                        setup.imageSourceDesc.rect.y,
                                        setup.imageSourceDesc.rect.width,
                                        setup.imageSourceDesc.rect.height);
    }

    // setup preprocessed tensor data
    setup.imageDestinationDesc.type = inputTensor.valueType;
    setup.imageDestinationDesc.surfaceWidth = modelWidth;
    setup.imageDestinationDesc.surfaceHeight = modelHeight;
    setup.imageDestinationDesc.rect = {0, 0, modelWidth, modelHeight};
    setup.imageDestinationDesc.kind = inputTensor.dataKind;
    setup.imageDestinationDesc.planeCount = 1;
    setup.imageDestinationDesc.planes[0] = {
        nullptr,
        upcomingTensorAddresses[inputImageTensorIndex],
        inputTensor.shape.getFullValueCount() * opk::getValueTypeByteSize(inputTensor.valueType),
        0,
    };
    if (setup.imageDestinationDesc.planes[0].data ||
        !setup.imageDestinationDesc.planes[0].mutableData) {
        opk::log::error("GenericImagePreprocessOp input tensor {} is not writable\n",
                        inputImageTensorIndex);
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "GenericImagePreprocessOp input tensor " +
                                            std::to_string(inputImageTensorIndex) +
                                            " is not writable"));
    }
    setup.imageDestinationDesc.keepAspectRatio = inputTensor.keepAspectRatio;
    setup.imageDestinationDesc.letterboxRed = inputTensor.letterboxRed;
    setup.imageDestinationDesc.letterboxGreen = inputTensor.letterboxGreen;
    setup.imageDestinationDesc.letterboxBlue = inputTensor.letterboxBlue;

    opk::PixelRect letterboxInnerRect = setup.imageDestinationDesc.rect;
    if (inputTensor.keepAspectRatio) {
        letterboxInnerRect =
            opk::computeLetterboxInnerRect(cropRect, setup.imageDestinationDesc.rect);
    }

    // call tensor building
    opk::Result<void> result = genericImageInputTensorBuilder.build(setup);
    if (result.has_value() == false) {
        return tl::unexpected(result.error());
    }

    // debug
    if (false) {
        std::string debugFile =
            (debugOutputDirectory() / fmt::format("tensor_[{}][{}]_{}x{}.png",
                                                  upcomingInferenceModel.contentType,
                                                  opk::nextObjectId(),
                                                  modelWidth,
                                                  modelHeight))
                .string();

        opk::Tools::savePngFromRgbChwF32(debugFile,
                                         (float *)upcomingTensorAddresses[inputImageTensorIndex],
                                         modelWidth,
                                         modelHeight);
    }

    // populate inference info
    opChainContext.inferenceInfo.image.width = cropRect.width;
    opChainContext.inferenceInfo.image.height = cropRect.height;
    opChainContext.inferenceInfo.image.modelWidth = modelWidth;
    opChainContext.inferenceInfo.image.modelHeight = modelHeight;
    opChainContext.inferenceInfo.image.letterboxLeft =
        letterboxInnerRect.x - setup.imageDestinationDesc.rect.x;
    opChainContext.inferenceInfo.image.letterboxRight =
        setup.imageDestinationDesc.rect.width - letterboxInnerRect.width -
        opChainContext.inferenceInfo.image.letterboxLeft;
    opChainContext.inferenceInfo.image.letterboxTop =
        letterboxInnerRect.y - setup.imageDestinationDesc.rect.y;
    opChainContext.inferenceInfo.image.letterboxBottom =
        setup.imageDestinationDesc.rect.height - letterboxInnerRect.height -
        opChainContext.inferenceInfo.image.letterboxTop;
    opChainContext.inferenceInfo.modelName = upcomingInferenceModel.name;
    opChainContext.inferenceInfo.contentType = upcomingInferenceModel.contentType;
    opChainContext.inferenceInfo.parentId = sourceId;

    return opk::op::OpSignal::Continue;
}
