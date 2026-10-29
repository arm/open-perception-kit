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
#include "Log.h"
#include "op/GpuPreprocess.h"
#include "op/Op.h"
#include "op/VulkanInput.h"
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
        tensorIndex = resolveTensorIndex();
        const auto bytes{validatedTensorBytes()};
        if (!bytes)
            return tl::unexpected(bytes.error());
        tensorBytes = *bytes;
        if (auto *const backend = ops[index + 1]->as<op::OpInterfaceVulkanInput>()) {
            const auto target = backend->getVulkanInput(tensorIndex);
            if (!target) {
                return tl::unexpected(target.error());
            }
            if (!*target || (*target)->bytes != tensorBytes) {
                return failure("Vulkan inference input size does not match preprocessing");
            }
            vulkanInput = *target;
        }
        return {};
    }

    Result<void> prepareDevice() override {
        try {
            gpu = vulkanInput ? std::make_shared<GpuPreprocess>(vulkanInput)
                              : std::make_shared<GpuPreprocess>(tensorBytes);
        } catch (const std::exception &error) {
            return failure(error.what());
        }
        return {};
    }

    void beginFrame() override {
        preparedFrame = nullptr;
        preparedSource = nullptr;
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
        const auto result{preprocessCrop(context, frame, crop, inner, input)};
        if (!result)
            return tl::unexpected(result.error());
        context.inferenceImageCrops.pop_back();
        context.inferenceImageCropIds.pop_back();
        context.inferenceInfo.image = imageMetadata(crop, destination, inner);
        context.inferenceInfo.modelName = model.name;
        context.inferenceInfo.contentType = model.contentType;
        context.inferenceInfo.parentId = sourceId;
        reportTransfers();
        return op::OpSignal::Continue;
    }

  private:
    void reportTransfers() {
        if (transferReported)
            return;
        log::notice("{}: GPU preprocessing on {}; source {}, tensor {}.\n",
                    instanceId,
                    gpu->identity(),
                    copiedInput ? "uploaded" : "shared",
                    copiedTensor ? "copied" : "shared");
        transferReported = true;
    }

    size_t resolveTensorIndex() const {
        if (tensorIndex == InvalidTensorIndex) {
            for (size_t candidate{0}; candidate < model.inputs.size(); ++candidate) {
                size_t width{0}, height{0};
                if (model.inputs[candidate].tryGetImageTensorSize(width, height)) {
                    return candidate;
                }
            }
        }
        return tensorIndex;
    }

    Result<size_t> validatedTensorBytes() const {
        if (tensorIndex >= model.inputs.size()) {
            return tl::unexpected(failure("No image input tensor was found").error());
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
            return tl::unexpected(
                failure("GPU preprocessing needs a fixed, single-image RGB float32 CHW/HWC input")
                    .error());
        }
        if (!std::isfinite(input.mean.r) || !std::isfinite(input.mean.g) ||
            !std::isfinite(input.mean.b) || !std::isfinite(input.std.r) ||
            !std::isfinite(input.std.g) || !std::isfinite(input.std.b) ||
            !std::isfinite(input.letterboxRed) || !std::isfinite(input.letterboxGreen) ||
            !std::isfinite(input.letterboxBlue)) {
            return tl::unexpected(failure("Model preprocessing values must be finite").error());
        }
        return width * height * rgbChannels * sizeof(float);
    }

    Result<void> bindInferenceTensor(const std::span<uint8_t> data) {
        bool tensorWasCopied{gpu->tensorCopied()};
        const bool bound{sharedInput && sharedInput->setSharedInputStorage(tensorIndex, data, gpu)};
        if (!bound) {
            auto *const destinationAddress{inference->getTensorDataAddress(tensorIndex)};
            if (!destinationAddress)
                return failure("Inference input storage is not writable");
            if (destinationAddress != data.data())
                std::memcpy(destinationAddress, data.data(), data.size());
            tensorWasCopied = true;
        }
        copiedTensor = tensorWasCopied;
        return {};
    }

    Result<void> preprocessCrop(op::OpChainContext &context,
                                const mediaio::VideoFrame *const frame,
                                const PixelRect &crop,
                                const PixelRect &inner,
                                const ModelInput &input) {
        try {
            if (preparedFrame != frame) {
                copiedInput = !gpu->import(*frame);
                preparedSource = frame;
                if (copiedInput) {
                    preparedSource = context.getReadableVideoFrame(sourceName);
                    if (!preparedSource)
                        return failure("GPU input cannot be mapped for upload");
                    gpu->upload(*preparedSource);
                }
                preparedFrame = frame;
            }
            const auto data{gpu->run(*preparedSource, crop, inner, input)};
            if (!vulkanInput) {
                const auto bound{bindInferenceTensor(data)};
                if (!bound) {
                    return bound;
                }
            }
        } catch (const std::exception &error) {
            return failure(error.what());
        }
        return {};
    }

    static ImageInferenceMetadata
    imageMetadata(const PixelRect &crop, const PixelRect &destination, const PixelRect &inner) {
        ImageInferenceMetadata image{};
        image.width = crop.width;
        image.height = crop.height;
        image.modelWidth = destination.width;
        image.modelHeight = destination.height;
        image.letterboxLeft = inner.x;
        image.letterboxRight = destination.width - inner.x - inner.width;
        image.letterboxTop = inner.y;
        image.letterboxBottom = destination.height - inner.y - inner.height;
        return image;
    }

    static Result<void> failure(const std::string &message) {
        return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData, message));
    }
    std::string sourceName;
    size_t tensorIndex{InvalidTensorIndex};
    size_t tensorBytes{0};
    Model model;
    op::OpInterfaceSharedInput *sharedInput{nullptr};
    const op::OpInterfaceInference *inference{nullptr};
    // Borrowed from the current context; beginFrame prevents reuse across executions.
    const mediaio::VideoFrame *preparedFrame{nullptr};
    const mediaio::VideoFrame *preparedSource{nullptr};
    bool copiedInput{false};
    bool copiedTensor{false};
    bool transferReported{false};
    std::shared_ptr<const Input> vulkanInput;
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
