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

#include "PaddleOcrPreprocessOp.h"

#include "mediaio/VideoFrame.h"
#include "opk/ImageOpDesc.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace opk::stdop {
namespace {

bool hasReadableBgraPlane(const opk::mediaio::VideoFrame &frame) {
    const auto planes = frame.planes();
    return planes.size() == 1 && planes[0].hasHostData() && planes[0].canRead();
}

opk::Result<void> validateFrame(const opk::mediaio::VideoFrame &frame, const opk::PixelRect &crop) {
    if (frame.format() != opk::RawImagePixelFormat::Bgra || !hasReadableBgraPlane(frame))
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "PaddleOcrPreprocess needs one readable BGRA plane"));
    if (crop.isEmpty() || !crop.fitsWithin(frame.width(), frame.height()))
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "PaddleOcrPreprocess crop is empty or outside the frame"));

    const auto &plane = frame.planes()[0];
    const size_t width = frame.width();
    const size_t height = frame.height();
    const size_t stride = plane.strideBytes();
    if (width > std::numeric_limits<size_t>::max() / 4 || stride < width * 4 ||
        height - 1 > (std::numeric_limits<size_t>::max() - width * 4) / stride ||
        plane.byteSize() < (height - 1) * stride + width * 4)
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "PaddleOcrPreprocess BGRA stride or plane size is invalid"));
    return {};
}

struct LinearSample {
    size_t first;
    size_t second;
    float weight;
};

LinearSample sampleAxis(size_t position, size_t sourceSize, size_t destinationSize) {
    // Half-pixel centers with replicated border pixels, as in standard linear resize.
    const double coordinate =
        std::clamp((static_cast<double>(position) + 0.5) * static_cast<double>(sourceSize) /
                           static_cast<double>(destinationSize) -
                       0.5,
                   0.0,
                   static_cast<double>(sourceSize - 1));
    const auto first = static_cast<size_t>(coordinate);
    return {first, std::min(first + 1, sourceSize - 1), static_cast<float>(coordinate - first)};
}

void writeTensor(const opk::mediaio::VideoFrame &frame,
                 const opk::PixelRect &crop,
                 const opk::PixelRect &inner,
                 const opk::ModelInput &input,
                 float paddingValue,
                 float *output) {
    const size_t width = input.shape.dims[3];
    const size_t height = input.shape.dims[2];
    const size_t planeSize = width * height;
    std::fill_n(output, 3 * planeSize, paddingValue);
    const std::array<float, 3> mean{input.mean.r, input.mean.g, input.mean.b};
    const std::array<float, 3> std{input.std.r, input.std.g, input.std.b};
    const auto &plane = frame.planes()[0];
    const auto *data = static_cast<const uint8_t *>(plane.data());
    const size_t stride = plane.strideBytes();

    // Reuse the horizontal sampling map across every row and channel.
    std::vector<LinearSample> horizontal(inner.width);
    for (size_t x = 0; x < inner.width; ++x)
        horizontal[x] = sampleAxis(x, crop.width, inner.width);
    for (size_t y = 0; y < inner.height; ++y) {
        const auto sy = sampleAxis(y, crop.height, inner.height);
        const auto *top = data + (crop.y + sy.first) * stride + crop.x * 4;
        const auto *bottom = data + (crop.y + sy.second) * stride + crop.x * 4;
        for (size_t x = 0; x < inner.width; ++x) {
            const auto sx = horizontal[x];
            for (size_t channel = 0; channel < 3; ++channel) {
                const float a = std::lerp(static_cast<float>(top[sx.first * 4 + channel]),
                                          static_cast<float>(top[sx.second * 4 + channel]),
                                          sx.weight);
                const float b = std::lerp(static_cast<float>(bottom[sx.first * 4 + channel]),
                                          static_cast<float>(bottom[sx.second * 4 + channel]),
                                          sx.weight);
                // Paddle resizes uint8 images before converting to float32.
                const float pixel = std::round(std::lerp(a, b, sy.weight));
                const float value = (pixel / 255.0f - mean[channel]) / std[channel];
                output[channel * planeSize + (inner.y + y) * width + inner.x + x] = value;
            }
        }
    }
}

} // namespace

opk::Result<void> PaddleOcrPreprocessOp::configure(const opk::AttributeMap &attributes) {
    mode = Mode::Unconfigured;
    inferenceOp = nullptr;
    for (const auto &[key, value] : attributes.raw()) {
        if (key != "mode" && key != "paddingValue" && key != "inputImageSourceName" &&
            key != "inputImageTensorIndex")
            return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                            "PaddleOcrPreprocess unknown attribute: " + key));
        if (((key == "mode" || key == "inputImageSourceName") && !value.isString()) ||
            (key == "inputImageTensorIndex" && !value.isInt()) ||
            (key == "paddingValue" && !value.isInt() && !value.isDouble()))
            return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                            "PaddleOcrPreprocess invalid attribute type: " + key));
    }
    const auto requestedMode = attributes.getStringOrDefault("mode", "");
    if (requestedMode != "detection" && requestedMode != "recognition")
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "PaddleOcrPreprocess mode must be detection or recognition"));
    if (!attributes.contains("paddingValue"))
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                        "PaddleOcrPreprocess paddingValue is required"));
    const double padding = attributes.getDouble("paddingValue");
    if (!std::isfinite(padding) || std::abs(padding) > std::numeric_limits<float>::max())
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                        "PaddleOcrPreprocess paddingValue must be finite float32"));
    const auto source = attributes.getStringOrDefault("inputImageSourceName", "pipelineVideoFrame");
    if (source.empty() || std::isspace(static_cast<unsigned char>(source.front())) ||
        std::isspace(static_cast<unsigned char>(source.back())))
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "PaddleOcrPreprocess image source must be nonempty and trimmed"));
    const auto tensorIndex = attributes.getIntOrDefault("inputImageTensorIndex", 0);
    if (tensorIndex < 0 || tensorIndex >= static_cast<int64_t>(opk::MaxTensorCount))
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                        "PaddleOcrPreprocess tensor index is out of range"));

    inputImageSourceName = source;
    inputImageTensorIndex = static_cast<size_t>(tensorIndex);
    paddingValue = static_cast<float>(padding);
    mode = requestedMode == "detection" ? Mode::Detection : Mode::Recognition;
    return {};
}

opk::Result<void> PaddleOcrPreprocessOp::bind(size_t index, const std::vector<opk::op::Op *> &ops) {
    inferenceOp = nullptr;
    if (mode == Mode::Unconfigured || index >= ops.size() || index + 1 >= ops.size() ||
        ops[index + 1] == nullptr)
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "PaddleOcrPreprocess must be configured and followed by inference"));
    const auto *next = ops[index + 1]->as<opk::op::OpInterfaceInference>();
    if (next == nullptr || inputImageTensorIndex >= next->getModel().inputs.size())
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "PaddleOcrPreprocess cannot find the next inference input"));
    const auto &input = next->getModel().inputs[inputImageTensorIndex];
    if (input.shape.rank != 4 || input.shape.dims[0] != 1 || input.shape.dims[1] != 3 ||
        input.shape.dims[2] <= 0 || input.shape.dims[3] <= 0 ||
        input.valueType != opk::Dtype::Float32 || input.dataKind != opk::DataKind::ImageBgrChw ||
        input.matchShapeOutputIndex != opk::InvalidTensorIndex)
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "PaddleOcrPreprocess requires a fixed ImageBgrChw Float32 input [1,3,H,W]"));
    if (static_cast<size_t>(input.shape.dims[2]) > std::numeric_limits<size_t>::max() /
                                                       sizeof(float) / 3 /
                                                       static_cast<size_t>(input.shape.dims[3]))
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                        "PaddleOcrPreprocess input tensor is too large"));
    const std::array<float, 3> mean{input.mean.r, input.mean.g, input.mean.b};
    const std::array<float, 3> std{input.std.r, input.std.g, input.std.b};
    for (size_t channel = 0; channel < 3; ++channel) {
        if (!std::isfinite(mean[channel]) || !std::isfinite(std[channel]) || std[channel] <= 0.0f)
            return tl::unexpected(OPK_ERROR(
                opk::ErrorFlag::InvalidOpChain,
                "PaddleOcrPreprocess requires finite mean and positive std in BGR order"));
    }
    if (next->getTensorDataAddress(inputImageTensorIndex) == nullptr)
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                        "PaddleOcrPreprocess inference input buffer is missing"));
    model = next->getModel();
    inferenceOp = next;
    return {};
}

opk::Result<opk::op::OpSignal> PaddleOcrPreprocessOp::process(opk::op::OpChainContext &context) {
    if (inferenceOp == nullptr)
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                        "PaddleOcrPreprocess is not bound to inference"));
    if (context.inferenceImageCrops.size() != context.inferenceImageCropIds.size())
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "PaddleOcrPreprocess crop and object ID counts differ"));
    if (context.inferenceImageCrops.empty())
        return opk::op::OpSignal::BreakLoop;

    const auto crop = context.inferenceImageCrops.back();
    const auto *frame = context.getVideoFrame(inputImageSourceName);
    if (frame == nullptr)
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "PaddleOcrPreprocess missing VideoFrame: " + inputImageSourceName));
    std::unique_ptr<opk::mediaio::VideoFrame> mappedFrame;
    if (!hasReadableBgraPlane(*frame)) {
        mappedFrame = frame->map(opk::AccessMode::Read);
        if (!mappedFrame)
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidData,
                          "PaddleOcrPreprocess could not map the image for reading"));
        frame = mappedFrame.get();
    }
    if (const auto valid = validateFrame(*frame, crop); !valid)
        return tl::unexpected(valid.error());
    auto *buffer = inferenceOp->getTensorDataAddress(inputImageTensorIndex);
    if (buffer == nullptr || reinterpret_cast<uintptr_t>(buffer) % alignof(float) != 0)
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "PaddleOcrPreprocess needs an aligned float32 input buffer"));

    const bool detection = mode == Mode::Detection;
    const auto &input = model.inputs[inputImageTensorIndex];
    const size_t width = input.shape.dims[3];
    const size_t height = input.shape.dims[2];
    opk::PixelRect inner{0, 0, width, height};
    if (detection) {
        inner = opk::computeLetterboxInnerRect(crop, inner);
    } else {
        const double scaledWidth =
            std::ceil(static_cast<double>(height) * static_cast<double>(crop.width) /
                      static_cast<double>(crop.height));
        inner.width = static_cast<size_t>(std::clamp(scaledWidth, 1.0, static_cast<double>(width)));
    }
    writeTensor(*frame, crop, inner, input, paddingValue, reinterpret_cast<float *>(buffer));

    context.inferenceInfo.image = {crop.width,
                                   crop.height,
                                   width,
                                   height,
                                   inner.x,
                                   width - inner.x - inner.width,
                                   inner.y,
                                   height - inner.y - inner.height};
    context.inferenceInfo.modelName = model.name;
    context.inferenceInfo.contentType = model.contentType;
    context.inferenceInfo.parentId = context.inferenceImageCropIds.back();
    context.inferenceImageCrops.pop_back();
    context.inferenceImageCropIds.pop_back();
    return opk::op::OpSignal::Continue;
}

} // namespace opk::stdop
