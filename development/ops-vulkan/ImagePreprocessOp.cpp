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

#include "GpuPreprocess.h"
#include "op/GpuPreprocess.h"
#include "op/Op.h"
#include "perf/PerformanceMetrics.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace opk::vulkan {
namespace {
constexpr size_t rgbChannels{3};
constexpr size_t imageTensorRank{4};

class ImagePreprocessOp final : public op::Op, public op::OpInterfaceGpuPreprocess {
  public:
    Result<void> configure(const AttributeMap &attributes) override {
        for (const auto &[name, value] : attributes.raw()) {
            if (name != "inputImageSourceName" && name != "inputImageTensorIndex") {
                return failure("Unknown GPU preprocessing attribute: " + name);
            }
            if (name == "inputImageTensorIndex" && (!value.isInt() || value.asInt() < 0)) {
                return failure("inputImageTensorIndex must be a nonnegative integer");
            }
            if (name != "inputImageTensorIndex" && !value.isString()) {
                return failure(name + " must be a string");
            }
        }
        sourceName = attributes.getStringOrDefault("inputImageSourceName", "pipelineVideoFrame");
        tensorIndex = attributes.getIntOrDefault("inputImageTensorIndex", InvalidTensorIndex);
        return {};
    }

    Result<void> bind(const size_t index, const std::vector<op::Op *> &ops) override {
        if (index >= ops.size() || ops.size() - index <= 1) {
            return failure("GPU preprocessing must precede inference");
        }
        inference = ops[index + 1]->as<op::OpInterfaceInference>();
        sharedInput = ops[index + 1]->as<op::OpInterfaceSharedInput>();
        if (!inference) {
            return failure("GPU preprocessing needs an inference backend");
        }
        model = inference->getModel();
        if (tensorIndex == InvalidTensorIndex) {
            for (size_t candidate{0}; candidate < model.inputs.size(); ++candidate) {
                size_t width{0}, height{0};
                if (model.inputs[candidate].tryGetImageTensorSize(width, height)) {
                    tensorIndex = candidate;
                    break;
                }
            }
        }
        if (tensorIndex >= model.inputs.size()) {
            return failure("No image input tensor was found");
        }
        const auto &input{model.inputs[tensorIndex]};
        size_t width{0}, height{0};
        if (input.valueType != Dtype::Float32 ||
            (input.dataKind != DataKind::ImageRgbChw && input.dataKind != DataKind::ImageRgbHwc) ||
            input.shape.rank != imageTensorRank || input.shape.dims[0] != 1 ||
            input.shape.hasDynamicDimension() || !input.tryGetImageTensorSize(width, height) ||
            width == 0 || height == 0 || input.matchShapeOutputIndex != InvalidTensorIndex ||
            width > std::numeric_limits<size_t>::max() / height / rgbChannels / sizeof(float) ||
            input.shape.getFullValueCount() != width * height * rgbChannels) {
            return failure(
                "GPU preprocessing needs a fixed, single-image RGB float32 CHW/HWC input");
        }
        if (!std::isfinite(input.mean.r) || !std::isfinite(input.mean.g) ||
            !std::isfinite(input.mean.b) || !std::isfinite(input.std.r) ||
            !std::isfinite(input.std.g) || !std::isfinite(input.std.b) ||
            !std::isfinite(input.letterboxRed) || !std::isfinite(input.letterboxGreen) ||
            !std::isfinite(input.letterboxBlue)) {
            return failure("Model preprocessing values must be finite");
        }
        tensorBytes = width * height * rgbChannels * sizeof(float);
        return {};
    }

    Result<void> prepareDevice() override {
        try {
            gpu = std::make_shared<GpuPreprocess>(tensorBytes);
        } catch (const std::exception &error) {
            return failure(error.what());
        }
        return {};
    }

    Result<op::OpSignal> process(op::OpChainContext &context) override {
        OPK_PERF_SCOPE(fmt::format("vulkan/GenImgPre/{}", model.name));
        if (context.inferenceImageCrops.empty()) {
            return op::OpSignal::BreakLoop;
        }
        if (!gpu || context.inferenceImageCropIds.empty()) {
            return tl::unexpected(
                failure("GPU preprocessing has no valid inference context").error());
        }
        const auto *const frame{context.getVideoFrame(sourceName)};
        if (!frame) {
            return tl::unexpected(
                failure("GPU preprocessing needs VideoFrame '" + sourceName + "'").error());
        }
        const PixelRect crop{context.inferenceImageCrops.back()};
        if (crop.isEmpty())
            return tl::unexpected(failure("GPU input crop is empty").error());
        const auto sourceId{context.inferenceImageCropIds.back()};
        const auto &input{model.inputs[tensorIndex]};
        size_t width{0}, height{0};
        input.tryGetImageTensorSize(width, height);
        const PixelRect destination{0, 0, width, height};
        const PixelRect inner{input.keepAspectRatio ? computeLetterboxInnerRect(crop, destination)
                                                    : destination};
        try {
            const auto *const source{context.getReadableVideoFrame(sourceName)};
            if (!source)
                return tl::unexpected(failure("GPU input cannot be mapped for upload").error());
            gpu->upload(*source);
            const auto data{gpu->run(*source, crop, inner, input)};
            bool tensorWasCopied{gpu->tensorCopied()};
            const bool bound{sharedInput &&
                             bool(sharedInput->bindInputTensor(tensorIndex, data, gpu))};
            if (!bound) {
                auto *const destinationAddress{inference->getTensorDataAddress(tensorIndex)};
                if (!destinationAddress)
                    return tl::unexpected(
                        failure("Inference input storage is not writable").error());
                if (destinationAddress != data.data())
                    std::memcpy(destinationAddress, data.data(), data.size());
                tensorWasCopied = true;
            }
            copiedInput = true;
            copiedTensor = tensorWasCopied;
        } catch (const std::exception &error) {
            return tl::unexpected(failure(error.what()).error());
        }
        context.inferenceImageCrops.pop_back();
        context.inferenceImageCropIds.pop_back();
        auto &image{context.inferenceInfo.image};
        image.width = crop.width;
        image.height = crop.height;
        image.modelWidth = width;
        image.modelHeight = height;
        image.letterboxLeft = inner.x;
        image.letterboxRight = width - inner.x - inner.width;
        image.letterboxTop = inner.y;
        image.letterboxBottom = height - inner.y - inner.height;
        context.inferenceInfo.modelName = model.name;
        context.inferenceInfo.contentType = model.contentType;
        context.inferenceInfo.parentId = sourceId;
        return op::OpSignal::Continue;
    }

    std::string identity() const override {
        return gpu ? gpu->identity() : "vulkan/uninitialized";
    }
    bool inputCopied() const override {
        return copiedInput;
    }
    bool tensorCopied() const override {
        return copiedTensor;
    }

  private:
    static Result<void> failure(const std::string &message) {
        return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData, message));
    }
    std::string sourceName;
    size_t tensorIndex{InvalidTensorIndex};
    size_t tensorBytes{0};
    Model model;
    op::OpInterfaceSharedInput *sharedInput{nullptr};
    const op::OpInterfaceInference *inference{nullptr};
    bool copiedInput{false};
    bool copiedTensor{false};
    std::shared_ptr<GpuPreprocess> gpu;
};
} // namespace
} // namespace opk::vulkan

extern "C" void *opk_create_op_instance(const char *const name) {
    if (name && std::string_view(name) == "GenericImagePreprocess") {
        return new opk::vulkan::ImagePreprocessOp();
    }
    return nullptr;
}

extern "C" void opk_delete_op_instance(void *const instance) {
    delete static_cast<opk::op::Op *>(instance);
}
