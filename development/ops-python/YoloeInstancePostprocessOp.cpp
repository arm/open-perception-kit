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

#include "YoloeInstancePostprocessOp.h"
#include "YoloePreparedPrompt.h"
#include "opk/ImageResize.h"
#include "opk/QueryPromptInputs.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace opk::python {
namespace {
constexpr size_t Side = 640;
constexpr size_t PrototypeSide = 160;
constexpr size_t Coefficients = 32;
constexpr size_t Anchors = 8400;
constexpr size_t MaxMaskBytes = 64 * 1024 * 1024;
struct Candidate {
    size_t anchor;
    size_t group;
    float score;
    float left;
    float top;
    float right;
    float bottom;
};
float overlap(const Candidate &a, const Candidate &b) {
    const double intersection =
        std::max(0.0, static_cast<double>(std::min(a.right, b.right)) - std::max(a.left, b.left)) *
        std::max(0.0, static_cast<double>(std::min(a.bottom, b.bottom)) - std::max(a.top, b.top));
    const double area =
        (static_cast<double>(a.right) - a.left) * (static_cast<double>(a.bottom) - a.top) +
        (static_cast<double>(b.right) - b.left) * (static_cast<double>(b.bottom) - b.top) -
        intersection;
    return area > 0 ? static_cast<float>(intersection / area) : 0;
}
} // namespace

Result<void> YoloeInstancePostprocessOp::configure(const AttributeMap &attributes) {
    try {
        preparationInstance = attributes.getString("preparationInstance");
        inferenceInstance = attributes.getString("inferenceInstance");
        const double confidence = attributes.getDoubleOrDefault("confidenceThreshold", 0.25);
        const double iou = attributes.getDoubleOrDefault("iouThreshold", 0.7);
        const auto maximum = attributes.getIntOrDefault("maxDetections", 100);
        if (preparationInstance.empty() || inferenceInstance.empty() ||
            !std::isfinite(confidence) || !std::isfinite(iou) || confidence < 0 || confidence > 1 ||
            iou < 0 || iou > 1 || maximum < 1 || maximum > 100) {
            return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidOpChain,
                                            "Invalid query postprocessing binding or thresholds"));
        }
        confidenceThreshold = static_cast<float>(confidence);
        iouThreshold = static_cast<float>(iou);
        maxDetections = static_cast<size_t>(maximum);
        return {};
    } catch (const std::exception &error) {
        return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidOpChain, error.what()));
    }
}

Result<void> YoloeInstancePostprocessOp::bind(const size_t index,
                                              const std::vector<op::Op *> &ops) {
    if (index == 0 || loopId != 0 || ops[index - 1]->loopId != 0 ||
        ops[index - 1]->instanceId != inferenceInstance ||
        !ops[index - 1]->as<op::OpInterfaceInference>()) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidOpChain,
                      "Query postprocessing requires adjacent matching inference"));
    }
    preparer = nullptr;
    for (size_t previous = 0; previous < index; ++previous) {
        if (ops[previous]->instanceId == preparationInstance) {
            preparer = ops[previous]->as<PreparedPromptProvider>();
        }
    }
    if (!preparer) {
        return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidOpChain,
                                        "Query postprocessor preparation provider is missing"));
    }
    return {};
}

std::vector<OutputCapability> YoloeInstancePostprocessOp::getProvidedOutputs() const {
    using namespace open_perception_kit::metadata;
    return {outputCapability<BoxDetectionsT>(),
            outputCapability<InstanceSegmentationMasksT>(),
            outputCapability<PromptAssociationT>()};
}

Result<op::OpSignal> YoloeInstancePostprocessOp::process(op::OpChainContext &context) {
    using namespace open_perception_kit::metadata;
    if (!context.inferencePrompt) {
        return op::OpSignal::Continue;
    }
    const auto state = preparer->preparedFor(context.inferencePrompt, YoloePreparationContract);
    const auto prepared = std::dynamic_pointer_cast<const YoloePreparedPrompt>(state);
    if (!prepared || prepared->snapshot != context.inferencePrompt ||
        prepared->contract != YoloePreparationContract || !context.frameResults ||
        context.inferenceOutputTensorCount != 2) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData,
                      "Query results require matching successful inference and prepared mapping"));
    }
    const size_t groups = prepared->groupIds.size();
    const auto &prediction = context.inferenceOutputTensors[0];
    const auto &prototype = context.inferenceOutputTensors[1];
    if (groups == 0 || groups > MaxQueryPromptGroups || !prediction.isValid() ||
        !prototype.isValid() || prediction.getValueType() != Dtype::Float32 ||
        prototype.getValueType() != Dtype::Float32 ||
        !(prediction.getShape() ==
          Shape(1, static_cast<int>(4 + groups + Coefficients), static_cast<int>(Anchors))) ||
        !(prototype.getShape() == Shape(1, 32, 160, 160))) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::TensorError,
                      "Query segmentation output contract differs from qualified tensors"));
    }
    const auto &image = context.inferenceInfo.image;
    if (image.modelWidth != Side || image.modelHeight != Side || image.width == 0 ||
        image.height == 0 || image.width > MaxMaskBytes / image.height || image.frameId == 0 ||
        context.inferenceInfo.parentId != image.frameId || image.width != image.frameWidth ||
        image.height != image.frameHeight || image.cropX != 0 || image.cropY != 0 ||
        image.letterboxLeft + image.letterboxRight >= Side ||
        image.letterboxTop + image.letterboxBottom >= Side) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData,
                      "Query output requires bounded full-target letterbox geometry"));
    }
    std::vector<Candidate> candidates;
    for (size_t anchor = 0; anchor < Anchors; ++anchor) {
        float score = 0;
        size_t group = 0;
        for (size_t category = 0; category < groups; ++category) {
            const float value = prediction.get((4 + category) * Anchors + anchor);
            if (!std::isfinite(value) || value < 0 || value > 1) {
                return tl::unexpected(
                    OPK_ERROR(ErrorFlag::InvalidData, "Query scores must be finite probabilities"));
            }
            if (value > score) {
                score = value;
                group = category;
            }
        }
        if (score <= confidenceThreshold) {
            continue;
        }
        const float x = prediction.get(anchor);
        const float y = prediction.get(Anchors + anchor);
        const float width = prediction.get(2 * Anchors + anchor);
        const float height = prediction.get(3 * Anchors + anchor);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
            !std::isfinite(height) || width <= 0 || height <= 0) {
            return tl::unexpected(
                OPK_ERROR(ErrorFlag::InvalidData, "Query boxes must have finite positive extents"));
        }
        const Candidate candidate{
            anchor, group, score, x - width / 2, y - height / 2, x + width / 2, y + height / 2};
        if (!std::isfinite(candidate.left) || !std::isfinite(candidate.top) ||
            !std::isfinite(candidate.right) || !std::isfinite(candidate.bottom)) {
            return tl::unexpected(
                OPK_ERROR(ErrorFlag::InvalidData, "Derived query box edges overflow"));
        }
        candidates.push_back(candidate);
    }
    std::stable_sort(candidates.begin(),
                     candidates.end(),
                     [](const Candidate &a, const Candidate &b) { return a.score > b.score; });
    std::vector<Candidate> retained;
    for (const auto &candidate : candidates) {
        if (std::none_of(retained.begin(), retained.end(), [&](const Candidate &other) {
                return candidate.group == other.group && overlap(candidate, other) > iouThreshold;
            })) {
            retained.push_back(candidate);
            if (retained.size() == maxDetections) {
                break;
            }
        }
    }
    const size_t maskBytes = image.width * image.height;
    if (!retained.empty() && retained.size() > MaxMaskBytes / maskBytes) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData, "Query masks exceed bounded output storage"));
    }
    std::vector<float> prototypes(Coefficients * PrototypeSide * PrototypeSide);
    for (size_t index = 0; index < prototypes.size(); ++index) {
        prototypes[index] = prototype.get(index);
        if (!std::isfinite(prototypes[index])) {
            return tl::unexpected(
                OPK_ERROR(ErrorFlag::InvalidData, "Query prototypes must be finite"));
        }
    }
    const auto producer = producerInfo(context.inferenceInfo.inferElementId,
                                       "YoloeInstancePostprocess",
                                       "opk-python-ops/YoloeInstancePostprocess");
    const auto layer =
        open_perception_kit::makeLayerInfo({.model = context.inferenceInfo.modelName,
                                            .inferElementId = context.inferenceInfo.inferElementId,
                                            .producer = &producer});
    BoxDetectionsT boxes;
    InstanceSegmentationMasksT masks;
    PromptAssociationT association;
    boxes.layer = std::make_unique<LayerInfoT>(*layer);
    masks.layer = std::make_unique<LayerInfoT>(*layer);
    association.layer = std::make_unique<LayerInfoT>(*layer);
    association.request =
        std::make_unique<RequestEnvelopeT>(context.inferencePrompt->copyRequest());
    association.session_id = context.inferencePrompt->sessionId;
    association.parent_id = context.inferenceInfo.parentId;
    const float scale =
        std::min(static_cast<float>(Side) / image.width, static_cast<float>(Side) / image.height);
    try {
        for (const auto &candidate : retained) {
            const float left = std::clamp((candidate.left - image.letterboxLeft) / scale,
                                          0.0F,
                                          static_cast<float>(image.width));
            const float right = std::clamp((candidate.right - image.letterboxLeft) / scale,
                                           0.0F,
                                           static_cast<float>(image.width));
            const float top = std::clamp((candidate.top - image.letterboxTop) / scale,
                                         0.0F,
                                         static_cast<float>(image.height));
            const float bottom = std::clamp((candidate.bottom - image.letterboxTop) / scale,
                                            0.0F,
                                            static_cast<float>(image.height));
            if (left >= right || top >= bottom) {
                continue;
            }
            std::array<float, Coefficients> coefficients{};
            for (size_t channel = 0; channel < Coefficients; ++channel) {
                coefficients[channel] =
                    prediction.get((4 + groups + channel) * Anchors + candidate.anchor);
                if (!std::isfinite(coefficients[channel])) {
                    return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData,
                                                    "Query mask coefficients must be finite"));
                }
            }
            std::vector<float> logits(PrototypeSide * PrototypeSide);
            for (size_t y = 0; y < PrototypeSide; ++y) {
                auto *const row = logits.data() + y * PrototypeSide;
                for (size_t x = 0; x < PrototypeSide; ++x) {
                    float value = 0;
                    if (x >= candidate.left / 4 && x < candidate.right / 4 &&
                        y >= candidate.top / 4 && y < candidate.bottom / 4) {
                        for (size_t channel = 0; channel < Coefficients; ++channel) {
                            value += coefficients[channel] *
                                     prototypes[channel * PrototypeSide * PrototypeSide +
                                                y * PrototypeSide + x];
                        }
                    }
                    if (!std::isfinite(value)) {
                        return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData,
                                                        "Reconstructed query mask is non-finite"));
                    }
                    row[x] = value;
                }
            }
            const auto upsampled = resizeBilinear(logits, PrototypeSide, PrototypeSide, Side, Side);
            if (!upsampled) {
                return tl::unexpected(upsampled.error());
            }
            const size_t activeWidth = Side - image.letterboxLeft - image.letterboxRight;
            const size_t activeHeight = Side - image.letterboxTop - image.letterboxBottom;
            Bitmap bitmap(Bitmap::Type::Uint8, image.width, image.height);
            for (size_t y = 0; y < image.height; ++y) {
                const size_t sourceY =
                    image.letterboxTop + nearestImageSample(y, activeHeight, image.height);
                for (size_t x = 0; x < image.width; ++x) {
                    const size_t sourceX =
                        image.letterboxLeft + nearestImageSample(x, activeWidth, image.width);
                    // Nearest sampling commutes with thresholding, avoiding a second full-size
                    // mask.
                    bitmap.set8(x, y, (*upsampled)[sourceY * Side + sourceX] > 0.0F ? 1 : 0);
                }
            }
            auto box = std::make_unique<BoxDetectionT>();
            box->object = open_perception_kit::makeObjectMeta(0, context.inferenceInfo.parentId);
            box->box = open_perception_kit::makeImageBoundingBox(
                image, left, top, right - left, bottom - top);
            box->confidence = candidate.score;
            auto mask = std::make_unique<InstanceSegmentationMaskT>();
            mask->object = open_perception_kit::makeObjectMeta(0, context.inferenceInfo.parentId);
            mask->bitmap = open_perception_kit::makeBitmapData(bitmap, image);
            mask->box = std::make_unique<BoundingBoxT>(*box->box);
            mask->confidence = candidate.score;
            if (!mask->bitmap) {
                return tl::unexpected(
                    OPK_ERROR(ErrorFlag::InvalidData, "Invalid query mask image space"));
            }
            for (const uint64_t objectId : {box->object->id, mask->object->id}) {
                auto match = std::make_unique<PromptObjectMatchT>();
                match->object_id = objectId;
                match->group_id = prepared->groupIds[candidate.group];
                association.matches.push_back(std::move(match));
            }
            boxes.detections.push_back(std::move(box));
            masks.masks.push_back(std::move(mask));
        }
    } catch (const std::exception &error) {
        return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData, error.what()));
    }
    context.frameResults->add(std::move(boxes));
    context.frameResults->add(std::move(masks));
    context.frameResults->add(std::move(association));
    return op::OpSignal::Continue;
}
} // namespace opk::python
