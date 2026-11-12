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

#pragma once

#include "open_perception_kit.h"
#include "opk/Bitmap.h"
#include "opk/Types.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace open_perception_kit {

using FrameResults = container::envelope;

std::unique_ptr<metadata::ObjectMetaT>
makeObjectMeta(uint64_t id = 0, uint64_t parentId = 0, uint64_t creationTsNs = 0);

struct LayerInfoDescriptor {
    std::string_view model{};
    std::string_view inferElementId{};
    std::string_view contentType{};
    std::string_view engine{};
    std::string_view tags{};
    std::string_view labelFamily{};
    std::string_view compositingMode{};
    const metadata::ProducerInfoT *producer = nullptr;
};

std::unique_ptr<metadata::LayerInfoT> makeLayerInfo(const LayerInfoDescriptor &descriptor);

std::unique_ptr<metadata::ProducerInfoT> makeProducerInfo(std::string_view instanceId,
                                                          std::string_view component,
                                                          std::string_view implementation);

std::unique_ptr<metadata::BoundingBoxT>
makeBoundingBox(float x, float y, float width, float height);

std::unique_ptr<metadata::ImageSpaceT> makeImageSpace(const opk::ImageInferenceMetadata &image);
std::unique_ptr<metadata::BoundingBoxT>
makeImageBoundingBox(const opk::ImageInferenceMetadata &image,
                     const float x,
                     const float y,
                     const float width,
                     const float height,
                     const bool normalized = false);
bool validImageSpace(const metadata::ImageSpaceT *const space) noexcept;
// Resolves one box/track observation by ID; duplicates remain ambiguous even
// when one observation has missing or invalid geometry. No content-type filter.
const metadata::BoundingBoxT *findSubjectBox(const FrameResults &frameResults, const uint64_t id);
// Returns x, y, width, height in the requested surface's pixels. No parent lookup.
std::optional<std::array<double, 4>> projectBoundingBox(const metadata::BoundingBoxT &box,
                                                        const uint32_t targetWidth,
                                                        const uint32_t targetHeight) noexcept;

// Source-aligned grid, or a model-space grid whose letterbox is removed first.
// Return null for invalid geometry or storage; never infer a basis from parent_id.
std::unique_ptr<metadata::BitmapDataT> makeBitmapData(const opk::Bitmap &bitmap,
                                                      const opk::ImageInferenceMetadata &image);
std::unique_ptr<metadata::BitmapDataT>
makeModelMaskBitmapData(const opk::Bitmap &bitmap, const opk::ImageInferenceMetadata &image);
bool validBitmapData(const metadata::BitmapDataT *const bitmap) noexcept;
bool validMask(const metadata::SegmentationMaskT &mask) noexcept;
bool validMask(const metadata::SemanticSegmentationMaskT &mask) noexcept;
bool validMask(const metadata::InstanceSegmentationMaskT &mask) noexcept;
std::optional<std::array<double, 4>> projectBitmap(const metadata::BitmapDataT &bitmap,
                                                   const uint32_t targetWidth,
                                                   const uint32_t targetHeight) noexcept;

// Display-only rows; numeric consumers read PerformanceMeasurements directly.
std::vector<std::pair<std::string, std::string>>
formatPerformanceRows(const metadata::PerformanceMeasurementsT &measurements);

template <typename Fn>
void forEachBoxDetectionWithContentType(const FrameResults &frameResults,
                                        const std::string &contentType,
                                        uint64_t id,
                                        Fn &&fn) {
    frameResults.for_each<metadata::BoxDetectionsT>([&contentType, id, &fn](const auto &payload) {
        if (!payload.layer || payload.layer->content_type != contentType) {
            return;
        }

        for (const auto &det : payload.detections) {
            if (!det || !det->object) {
                continue;
            }
            if (id != 0U && det->object->id != id) {
                continue;
            }
            fn(*det);
        }
    });
}

template <typename Fn>
void forEachBoxDetectionWithContentType(const FrameResults &frameResults,
                                        const std::string &contentType,
                                        Fn &&fn) {
    forEachBoxDetectionWithContentType(frameResults, contentType, 0U, std::forward<Fn>(fn));
}

std::vector<uint8_t> serialize(const FrameResults &frameResults);

} // namespace open_perception_kit
