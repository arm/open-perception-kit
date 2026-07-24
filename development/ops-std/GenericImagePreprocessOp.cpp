/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "GenericImagePreprocessOp.h"

#include <cstdint>
#include <fmt/core.h>
#include <memory>

#include "Log.h"
#include "pek/ImageOpDesc.h"
#include "pek/Perception.h"
#include "pek/Result.h"
#include "pek/TensorView.h"
#include "pek/Tools.h"
#include "pek/Types.h"
#include "tl/expected.hpp"

#include <perf/PerformanceTracer.h>

using namespace pek::stdop;

GenericImagePreprocessOp::GenericImagePreprocessOp() = default;
GenericImagePreprocessOp::~GenericImagePreprocessOp() = default;

pek::Result<void> GenericImagePreprocessOp::bind(size_t index,
                                                 const std::vector<pek::op::Op *> &ops) {
    // sanity check
    if (ops.size() <= index) {
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::InvalidOpChain,
                                        "GenericImagePreprocessOp cannot be the last Op"));
    }

    // sanity check (later maybe other ops will be valid between them)
    const pek::op::OpInterfaceInference *inferenceOp =
        ops[index + 1]->as<pek::op::OpInterfaceInference>();
    if (!inferenceOp) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidOpChain,
                      "GenericImagePreprocessOp should be BEFORE an inference Op"));
    }

    // --- grab infos about the upcoming inference
    upcomingInferenceModel = inferenceOp->getModel();
    for (size_t i = 0; i < upcomingInferenceModel.inputs.size(); i++) {
        upcomingTensorAddresses[i] = inferenceOp->getTensorDataAddress(i);
    }

    if (inputImageTensorIndex == pek::InvalidTensorIndex) {
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
    if (inputImageTensorIndex == pek::InvalidTensorIndex) {
        return tl::unexpected(PEK_ERROR(
            pek::ErrorFlag::InvalidOpChain,
            "GenericImagePreprocessOp cannot find an upcoming tensor index to work with"));
    }

    return {};
}

pek::Result<void> GenericImagePreprocessOp::configure(const pek::AttributeMap &attributes,
                                                      pek::op::OpSetupContext &setupContext) {
    // --- get config info from the json attributes
    inputImageSourceName =
        attributes.getStringOrDefault("inputImageSourceName", "pipelineVideoFrame");
    inputImageTensorIndex =
        attributes.getIntOrDefault("inputImageTensorIndex", pek::InvalidTensorIndex);
    return {};
}

pek::Result<pek::op::OpSignal>
GenericImagePreprocessOp::process(pek::op::OpChainContext &opChainContext) {
    PEK_TRACE_SCOPE(fmt::format("std/GenImgPre/{}", upcomingInferenceModel.modelFamily));

    if (opChainContext.inferenceImageCrops.size() == 0) {
        return pek::op::OpSignal::BreakLoop;
    }

    pek::PixelRect cropRect = opChainContext.inferenceImageCrops.back();
    opChainContext.inferenceImageCrops.pop_back();
    uint64_t sourceUuid = opChainContext.inferenceImageCropUuids.back();
    opChainContext.inferenceSourceUuid = opChainContext.inferenceImageCropUuids.back();
    opChainContext.inferenceImageCropUuids.pop_back();

    auto *pipelineVideoFrame = opChainContext.getVideoFrame(inputImageSourceName);

    if (pipelineVideoFrame == nullptr) {
        return tl::make_unexpected(PEK_ERROR(
            pek::ErrorFlag::InvalidOpChain,
            fmt::format("GenericImagePreprocessOp needs VideoFrame '{}'", inputImageSourceName)));
    }

    size_t modelWidth, modelHeight;
    const auto &inputTensor = upcomingInferenceModel.inputs[inputImageTensorIndex];
    if (false == inputTensor.tryGetImageTensorSize(modelWidth, modelHeight)) {
        return tl::make_unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidData, "tensor seems not to be an image"));
    }

    const pek::mediaio::VideoFrame *readableVideoFrame = pipelineVideoFrame;
    std::unique_ptr<pek::mediaio::VideoFrame> mappedPipelineVideoFrame;
    auto readablePlanes = readableVideoFrame->planes();
    if (readablePlanes.empty() || !readablePlanes.front().hasHostData() ||
        !readablePlanes.front().canRead()) {
        mappedPipelineVideoFrame = pipelineVideoFrame->map(pek::AccessMode::Read);
        if (!mappedPipelineVideoFrame) {
            return tl::make_unexpected(
                PEK_ERROR(pek::ErrorFlag::InvalidData,
                          "GenericImagePreprocessOp failed to map pipelineVideoFrame VideoFrame"));
        }

        readableVideoFrame = mappedPipelineVideoFrame.get();
        readablePlanes = readableVideoFrame->planes();
        if (readablePlanes.empty() || !readablePlanes.front().hasHostData() ||
            !readablePlanes.front().canRead()) {
            return tl::make_unexpected(
                PEK_ERROR(pek::ErrorFlag::InvalidData,
                          "GenericImagePreprocessOp VideoFrame has no readable host plane"));
        }
    }
    const auto &pipelineVideoPlane = readablePlanes.front();

    // setup tensor data source
    pek::TensorBuilder::Setup setup;
    setup.imageSourceDesc.data =
        const_cast<uint8_t *>(static_cast<const uint8_t *>(pipelineVideoPlane.data()));
    setup.imageSourceDesc.surfaceWidth = readableVideoFrame->width();
    setup.imageSourceDesc.surfaceHeight = readableVideoFrame->height();
    setup.imageSourceDesc.surfaceStride = pipelineVideoPlane.strideBytes();
    setup.imageSourceDesc.rect = cropRect;
    setup.imageSourceDesc.byteCount = pipelineVideoPlane.byteSize();
    setup.imageSourceDesc.kind = readableVideoFrame->format();
    setup.imageSourceDesc.type = pek::Dtype::Uint8;
    setup.imageSourceDesc.mean = inputTensor.mean;
    setup.imageSourceDesc.std = inputTensor.std;

    // debug
    if (false) {
        pek::log("crop: {} {} {} {}\n",
                 setup.imageSourceDesc.rect.x,
                 setup.imageSourceDesc.rect.y,
                 setup.imageSourceDesc.rect.width,
                 setup.imageSourceDesc.rect.height);
    }

    // debug
    if (false) {
        std::string debugFile = fmt::format("/work/var/crop_[{}]_{}_{}x{}x{}x{}.png",
                                            upcomingInferenceModel.contentType,
                                            pek::Uuid::next(),
                                            setup.imageSourceDesc.rect.x,
                                            setup.imageSourceDesc.rect.y,
                                            setup.imageSourceDesc.rect.width,
                                            setup.imageSourceDesc.rect.height);
        pek::Tools::savePngCropFromBgra(debugFile,
                                        setup.imageSourceDesc.data,
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
    setup.imageDestinationDesc.byteCount =
        inputTensor.shape.getFullValueCount() * pek::getValueTypeByteSize(inputTensor.valueType);
    setup.imageDestinationDesc.data = upcomingTensorAddresses[inputImageTensorIndex];
    setup.imageDestinationDesc.keepAspectRatio = inputTensor.keepAspectRatio;
    setup.imageDestinationDesc.letterboxRed = inputTensor.letterboxRed;
    setup.imageDestinationDesc.letterboxGreen = inputTensor.letterboxGreen;
    setup.imageDestinationDesc.letterboxBlue = inputTensor.letterboxBlue;

    pek::PixelRect letterboxInnerRect = setup.imageDestinationDesc.rect;
    if (inputTensor.keepAspectRatio) {
        letterboxInnerRect =
            pek::computeLetterboxInnerRect(cropRect, setup.imageDestinationDesc.rect);
    }

    // call tensor building
    pek::Result<void> result = genericImageInputTensorBuilder.build(setup);
    if (result.has_value() == false) {
        return tl::unexpected(result.error());
    }

    // debug
    if (false) {
        std::string debugFile = fmt::format("/work/var/tensor_[{}][{}]_{}x{}.png",
                                            upcomingInferenceModel.contentType,
                                            pek::Uuid::next(),
                                            modelWidth,
                                            modelHeight);

        pek::Tools::savePngFromRgbChwF32(debugFile,
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
    opChainContext.inferenceInfo.modelFamily = upcomingInferenceModel.modelFamily;
    opChainContext.inferenceInfo.contentType = upcomingInferenceModel.contentType;
    opChainContext.inferenceInfo.parentUuid = sourceUuid;

    return pek::op::OpSignal::Continue;
}
