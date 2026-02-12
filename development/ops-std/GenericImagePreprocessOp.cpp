#include "GenericImagePreprocessOp.h"

#include <cstdint>
#include <fmt/core.h>

#include "amp/Perception.h"
#include "amp/Result.h"
#include "amp/TensorView.h"
#include "amp/Tools.h"
#include "amp/Types.h"
#include "tl/expected.hpp"

#include <PerformanceTracer.h>

using namespace amp;

GenericImagePreprocessOp::GenericImagePreprocessOp() {}
GenericImagePreprocessOp::~GenericImagePreprocessOp() {}

amp::Result<void> GenericImagePreprocessOp::bind(size_t index, const std::vector<amp::Op *> &ops) {
    // sanity check
    if (ops.size() <= index) {
        return tl::unexpected(AMP_ERROR(amp::ErrorFlag::InvalidOpChain,
                                        "GenericImagePreprocessOp cannot be the last Op"));
    }

    // sanity check (later maybe other ops will be valid between them)
    const OpInterfaceInference *inferenceOp = ops[index + 1]->as<OpInterfaceInference>();
    if (!inferenceOp) {
        return tl::unexpected(
            AMP_ERROR(amp::ErrorFlag::InvalidOpChain,
                      "GenericImagePreprocessOp should be BEFORE an inference Op"));
    }

    // --- grab infos about the upcoming inference
    upcomingInferenceModel = inferenceOp->getModel();
    for (size_t i = 0; i < upcomingInferenceModel.inputs.size(); i++) {
        upcomingTensorAddresses[i] = inferenceOp->getTensorDataAddress(i);
    }

    if (inputImageTensorIndex == amp::InvalidTensorIndex) {
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
    if (inputImageTensorIndex == amp::InvalidTensorIndex) {
        return tl::unexpected(AMP_ERROR(
            amp::ErrorFlag::InvalidOpChain,
            "GenericImagePreprocessOp cannot find an upcoming tensor index to work with"));
    }

    return {};
}

amp::Result<void> GenericImagePreprocessOp::configure(const amp::AttributeMap &attributes) {
    // --- get config info from the json attributes
    inputImageSourceName =
        attributes.getStringOrDefault("inputImageSourceName", "pipelineVideoFrame");
    inputImageTensorIndex =
        attributes.getIntOrDefaultOrDefault("inputImageTensorIndex", amp::InvalidTensorIndex);
    return {};
}

amp::Result<void> GenericImagePreprocessOp::process(amp::OpChainContext &opChainContext) {
    AMP_TRACE_SCOPE(fmt::format("std/GenImgPre/{}", upcomingInferenceModel.modelFamily));

    if (opChainContext.inferenceCrops.size() == 0) {
        opChainContext.execute = false;
        return {};
    }

    amp::PixelRect cropRect = opChainContext.inferenceCrops.back();
    opChainContext.inferenceCrops.pop_back();
    uint64_t sourceUuid = opChainContext.inferenceCropUuids.back();
    opChainContext.inferenceSourceUuid = opChainContext.inferenceCropUuids.back();
    opChainContext.inferenceCropUuids.pop_back();

    amp::BitmapView *pipelineVideoFrame = opChainContext.getBitmapView("pipelineVideoFrame");

    if (pipelineVideoFrame == nullptr) {
        return tl::make_unexpected(AMP_ERROR(amp::ErrorFlag::InvalidOpChain,
                                             "GenericImagePreprocessOp needs pipelineVideoFrame"));
    }

    // setup tensor data source
    amp::TensorBuilder::Setup setup;
    setup.imageSource.data = pipelineVideoFrame->data;
    setup.imageSource.surfaceWidth = pipelineVideoFrame->width;
    setup.imageSource.surfaceHeight = pipelineVideoFrame->height;
    setup.imageSource.x = cropRect.x;
    setup.imageSource.y = cropRect.y;
    setup.imageSource.width = cropRect.width;
    setup.imageSource.height = cropRect.height;
    setup.imageSource.byteCount = pipelineVideoFrame->width * pipelineVideoFrame->height * 4;
    setup.imageSource.kind = amp::DataKind::ImageBgraHwc;
    setup.imageSource.type = amp::Tdt::Uint8;
    setup.imageSource.mean = upcomingInferenceModel.inputs[inputImageTensorIndex].mean;
    setup.imageSource.std = upcomingInferenceModel.inputs[inputImageTensorIndex].std;

    size_t modelWidth, modelHeight;
    if (false == upcomingInferenceModel.inputs[inputImageTensorIndex].tryGetImageTensorSize(
                     modelWidth, modelHeight)) {
        return tl::make_unexpected(
            AMP_ERROR(amp::ErrorFlag::InvalidData, "tensor seems not to be an image"));
    }

    // debug
    if (false) {
        fmt::print("crop: {} {} {} {}\n",
                   setup.imageSource.x,
                   setup.imageSource.y,
                   setup.imageSource.width,
                   setup.imageSource.height);
    }

    // debug
    if (false) {
        std::string debugFile = fmt::format("/work/var/crop_[{}]_{}_{}x{}x{}x{}.png",
                                            upcomingInferenceModel.contentType,
                                            (uint64_t)Uuid(),
                                            setup.imageSource.x,
                                            setup.imageSource.y,
                                            setup.imageSource.width,
                                            setup.imageSource.height);
        Tools::savePngCropFromBgra(debugFile,
                                   setup.imageSource.data,
                                   setup.imageSource.surfaceWidth,
                                   setup.imageSource.surfaceHeight,
                                   setup.imageSource.x,
                                   setup.imageSource.y,
                                   setup.imageSource.width,
                                   setup.imageSource.height);
    }

    // setup preprocessed tensor data
    setup.imageDestination.type = upcomingInferenceModel.inputs[inputImageTensorIndex].valueType;
    setup.imageDestination.surfaceWidth = modelWidth;
    setup.imageDestination.surfaceHeight = modelHeight;
    setup.imageDestination.x = 0;
    setup.imageDestination.y = 0;
    setup.imageDestination.width = modelWidth;
    setup.imageDestination.height = modelHeight;
    setup.imageDestination.kind = upcomingInferenceModel.inputs[inputImageTensorIndex].dataKind;
    setup.imageDestination.byteCount =
        upcomingInferenceModel.inputs[inputImageTensorIndex].shape.getFullValueCount() *
        amp::getValueTypeByteSize(upcomingInferenceModel.inputs[inputImageTensorIndex].valueType);
    setup.imageDestination.data = upcomingTensorAddresses[inputImageTensorIndex];

    // call tensor building
    amp::Result<void> result = genericImageInputTensorBuilder.build(setup);
    if (result.has_value() == false) {
        return result;
    }

    // debug
    if (false) {
        std::string debugFile = fmt::format("/work/var/tensor_[{}][{}]_{}x{}.png",
                                            upcomingInferenceModel.contentType,
                                            (uint64_t)Uuid(),
                                            modelWidth,
                                            modelHeight);

        Tools::savePngFromRgbChwF32(debugFile,
                                    (float *)upcomingTensorAddresses[inputImageTensorIndex],
                                    modelWidth,
                                    modelHeight);
    }

    // populate inference info
    opChainContext.inferenceInfo.image.width = cropRect.width;
    opChainContext.inferenceInfo.image.height = cropRect.height;
    opChainContext.inferenceInfo.image.modelWidth = modelWidth;
    opChainContext.inferenceInfo.image.modelHeight = modelHeight;
    opChainContext.inferenceInfo.modelFamily = upcomingInferenceModel.modelFamily;
    opChainContext.inferenceInfo.contentType = upcomingInferenceModel.contentType;
    opChainContext.inferenceInfo.parentUuid = sourceUuid;

    return {};
}
