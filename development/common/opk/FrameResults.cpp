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

#include "opk/FrameResults.h"

#include "opk/Tools.h"
#include "opk/Tracking.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <utility>

namespace open_perception_kit {

std::unique_ptr<metadata::ObjectMetaT>
makeObjectMeta(uint64_t id, uint64_t parentId, uint64_t creationTsNs) {
    auto object = std::make_unique<metadata::ObjectMetaT>();
    object->id = id == 0U ? opk::nextObjectId() : id;
    object->parent_id = parentId;
    object->creation_ts_ns = creationTsNs == 0U ? opk::Time::utcNano() : creationTsNs;
    return object;
}

std::unique_ptr<metadata::LayerInfoT> makeLayerInfo(const LayerInfoDescriptor &descriptor) {
    auto info = std::make_unique<metadata::LayerInfoT>();
    info->engine = descriptor.engine;
    info->model = descriptor.model;
    info->tags = descriptor.tags;
    info->infer_element_id = descriptor.inferElementId;
    info->label_family = descriptor.labelFamily;
    info->compositing_mode = descriptor.compositingMode;
    if (descriptor.producer != nullptr)
        info->producer = std::make_unique<metadata::ProducerInfoT>(*descriptor.producer);
    return info;
}

std::unique_ptr<metadata::ProducerInfoT> makeProducerInfo(std::string_view instanceId,
                                                          std::string_view component,
                                                          std::string_view implementation) {
    auto info = std::make_unique<metadata::ProducerInfoT>();
    info->instance_id = instanceId;
    info->component = component;
    info->implementation = implementation;
    return info;
}

std::unique_ptr<metadata::BoundingBoxT>
makeBoundingBox(float x, float y, float width, float height) {
    auto box = std::make_unique<metadata::BoundingBoxT>();
    box->x = x;
    box->y = y;
    box->width = width;
    box->height = height;
    return box;
}

std::unique_ptr<metadata::BitmapDataT> makeBitmapData(const opk::Bitmap &bitmap,
                                                      const opk::ImageInferenceMetadata &image) {
    if ((bitmap.getType() != opk::Bitmap::Type::Uint8 &&
         bitmap.getType() != opk::Bitmap::Type::Uint32) ||
        bitmap.getWidth() > std::numeric_limits<uint32_t>::max() ||
        bitmap.getHeight() > std::numeric_limits<uint32_t>::max())
        return nullptr;
    auto data = std::make_unique<metadata::BitmapDataT>();
    data->width = static_cast<uint32_t>(bitmap.getWidth());
    data->height = static_cast<uint32_t>(bitmap.getHeight());
    data->encoding = bitmap.getType() == opk::Bitmap::Type::Uint32
                         ? metadata::BitmapEncoding::Uint32LE
                         : metadata::BitmapEncoding::Uint8;
    const auto pixels = bitmap.getPixels();
    data->pixels.assign(pixels.begin(), pixels.end());
    data->space = makeImageSpace(image);
    if (!validBitmapData(data.get()))
        return nullptr;
    if (data->encoding == metadata::BitmapEncoding::Uint32LE) {
        for (size_t y = 0; y < data->height; ++y) {
            for (size_t x = 0; x < data->width; ++x) {
                const auto offset = (y * data->width + x) * 4;
                uint32_t value = 0;
                std::memcpy(&value, bitmap.getData() + offset, sizeof(value));
                for (size_t byte = 0; byte < 4; ++byte)
                    data->pixels[offset + byte] = static_cast<uint8_t>(value >> (8 * byte));
            }
        }
    }
    return data;
}

std::unique_ptr<metadata::BitmapDataT>
makeModelMaskBitmapData(const opk::Bitmap &bitmap, const opk::ImageInferenceMetadata &image) {
    auto data = makeBitmapData(bitmap, image);
    if (!data || data->encoding != metadata::BitmapEncoding::Uint8 || image.modelWidth == 0 ||
        image.modelHeight == 0 || image.letterboxLeft >= image.modelWidth ||
        image.letterboxRight >= image.modelWidth - image.letterboxLeft ||
        image.letterboxTop >= image.modelHeight ||
        image.letterboxBottom >= image.modelHeight - image.letterboxTop)
        return nullptr;
    if (image.letterboxLeft == 0 && image.letterboxRight == 0 && image.letterboxTop == 0 &&
        image.letterboxBottom == 0)
        return data;
    const double left = static_cast<double>(image.letterboxLeft) * data->width / image.modelWidth;
    const double top = static_cast<double>(image.letterboxTop) * data->height / image.modelHeight;
    const double width =
        static_cast<double>(image.modelWidth - image.letterboxLeft - image.letterboxRight) *
        data->width / image.modelWidth;
    const double height =
        static_cast<double>(image.modelHeight - image.letterboxTop - image.letterboxBottom) *
        data->height / image.modelHeight;
    const auto outputWidth = static_cast<uint32_t>(std::max(1.0, std::round(width)));
    const auto outputHeight = static_cast<uint32_t>(std::max(1.0, std::round(height)));
    std::vector<uint8_t> pixels(static_cast<size_t>(outputWidth) * outputHeight);
    for (size_t y = 0; y < outputHeight; ++y) {
        const auto sy = std::min(static_cast<size_t>(top + (y + .5) * height / outputHeight),
                                 static_cast<size_t>(data->height - 1));
        for (size_t x = 0; x < outputWidth; ++x) {
            const auto sx = std::min(static_cast<size_t>(left + (x + .5) * width / outputWidth),
                                     static_cast<size_t>(data->width - 1));
            pixels[y * outputWidth + x] = data->pixels[sy * data->width + sx];
        }
    }
    data->width = outputWidth;
    data->height = outputHeight;
    data->pixels = std::move(pixels);
    return data;
}

bool validBitmapData(const metadata::BitmapDataT *const bitmap) noexcept {
    if (!bitmap || !validImageSpace(bitmap->space.get()) || bitmap->width == 0 ||
        bitmap->height == 0)
        return false;
    const size_t bytes = bitmap->encoding == metadata::BitmapEncoding::Uint8      ? 1
                         : bitmap->encoding == metadata::BitmapEncoding::Uint32LE ? 4
                                                                                  : 0;
    return bytes != 0 &&
           bitmap->width <= std::numeric_limits<size_t>::max() / bytes / bitmap->height &&
           bitmap->pixels.size() == static_cast<size_t>(bitmap->width) * bitmap->height * bytes;
}

bool validMask(const metadata::SegmentationMaskT &mask) noexcept {
    return validBitmapData(mask.bitmap.get()) &&
           mask.bitmap->encoding == metadata::BitmapEncoding::Uint8 &&
           (mask.meaning == metadata::OpacityMeaning::BackgroundOpacity ||
            mask.meaning == metadata::OpacityMeaning::RegionOpacity);
}

bool validMask(const metadata::SemanticSegmentationMaskT &mask) noexcept {
    return validBitmapData(mask.bitmap.get()) && !mask.label_set.empty() &&
           (!mask.void_value || mask.bitmap->encoding == metadata::BitmapEncoding::Uint32LE ||
            *mask.void_value <= 255);
}

bool validMask(const metadata::InstanceSegmentationMaskT &mask) noexcept {
    if (!validBitmapData(mask.bitmap.get()) ||
        mask.bitmap->encoding != metadata::BitmapEncoding::Uint8 ||
        (mask.class_id && mask.label_set.empty()) ||
        (mask.confidence &&
         (!std::isfinite(*mask.confidence) || *mask.confidence < 0 || *mask.confidence > 1)))
        return false;
    if (mask.box &&
        (!projectBoundingBox(*mask.box, mask.bitmap->space->width, mask.bitmap->space->height) ||
         mask.box->space->frame_id != mask.bitmap->space->frame_id ||
         mask.box->space->width != mask.bitmap->space->width ||
         mask.box->space->height != mask.bitmap->space->height))
        return false;
    return std::all_of(mask.bitmap->pixels.begin(),
                       mask.bitmap->pixels.end(),
                       [](const uint8_t value) { return value <= 1; });
}

std::optional<std::array<double, 4>> projectBitmap(const metadata::BitmapDataT &bitmap,
                                                   const uint32_t targetWidth,
                                                   const uint32_t targetHeight) noexcept {
    if (!validBitmapData(&bitmap) || targetWidth == 0 || targetHeight == 0)
        return std::nullopt;
    const auto &space = *bitmap.space;
    const double sx = static_cast<double>(targetWidth) / space.width;
    const double sy = static_cast<double>(targetHeight) / space.height;
    return std::array<double, 4>{space.origin_x * sx,
                                 space.origin_y * sy,
                                 space.extent_width * sx,
                                 space.extent_height * sy};
}

std::unique_ptr<metadata::ImageSpaceT> makeImageSpace(const opk::ImageInferenceMetadata &image) {
    auto space = std::make_unique<metadata::ImageSpaceT>();
    space->frame_id = image.frameId;
    space->width = image.frameWidth;
    space->height = image.frameHeight;
    space->origin_x = static_cast<double>(image.cropX);
    space->origin_y = static_cast<double>(image.cropY);
    space->extent_width = static_cast<double>(image.width);
    space->extent_height = static_cast<double>(image.height);
    return space;
}

std::unique_ptr<metadata::BoundingBoxT>
makeImageBoundingBox(const opk::ImageInferenceMetadata &image,
                     const float x,
                     const float y,
                     const float width,
                     const float height,
                     const bool normalized) {
    auto box = makeBoundingBox(x, y, width, height);
    box->space = makeImageSpace(image);
    box->units =
        normalized ? metadata::CoordinateUnits::Normalized : metadata::CoordinateUnits::Pixels;
    return box;
}

bool validImageSpace(const metadata::ImageSpaceT *const space) noexcept {
    return space && space->frame_id != 0 && space->width > 0 && space->height > 0 &&
           std::isfinite(space->origin_x) && std::isfinite(space->origin_y) &&
           std::isfinite(space->extent_width) && std::isfinite(space->extent_height) &&
           space->origin_x >= 0 && space->origin_y >= 0 && space->extent_width > 0 &&
           space->extent_height > 0 && space->origin_x + space->extent_width <= space->width &&
           space->origin_y + space->extent_height <= space->height;
}

const metadata::BoundingBoxT *findSubjectBox(const FrameResults &frameResults, const uint64_t id) {
    if (id == 0U)
        return nullptr;
    const metadata::BoundingBoxT *box = nullptr;
    std::size_t count = 0U;
    const auto collect = [&box, &count, id](const auto &items) {
        for (const auto &item : items) {
            if (item && item->object && item->object->id == id) {
                ++count;
                box = item->box.get();
            }
        }
    };
    frameResults.for_each<metadata::BoxDetectionsT>(
        [&collect](const auto &p) { collect(p.detections); });
    frameResults.for_each<metadata::ObjectTracksT>([&](const auto &p) {
        for (const auto &track : p.tracks) {
            if (track && track->object && track->object->id == id) {
                ++count;
                box = opk::tracking::validTrack(*track) ? track->box.get() : nullptr;
            }
        }
    });
    return count == 1U ? box : nullptr;
}

std::optional<std::array<double, 4>> projectBoundingBox(const metadata::BoundingBoxT &box,
                                                        const uint32_t targetWidth,
                                                        const uint32_t targetHeight) noexcept {
    if (!validImageSpace(box.space.get()) || targetWidth == 0 || targetHeight == 0 ||
        !std::isfinite(box.x) || !std::isfinite(box.y) || !std::isfinite(box.width) ||
        !std::isfinite(box.height) || box.width <= 0 || box.height <= 0 ||
        (box.units != metadata::CoordinateUnits::Pixels &&
         box.units != metadata::CoordinateUnits::Normalized))
        return std::nullopt;
    const auto &space = *box.space;
    const double unitX =
        box.units == metadata::CoordinateUnits::Normalized ? space.extent_width : 1;
    const double unitY =
        box.units == metadata::CoordinateUnits::Normalized ? space.extent_height : 1;
    const double scaleX = static_cast<double>(targetWidth) / space.width;
    const double scaleY = static_cast<double>(targetHeight) / space.height;
    return std::array<double, 4>{(space.origin_x + box.x * unitX) * scaleX,
                                 (space.origin_y + box.y * unitY) * scaleY,
                                 box.width * unitX * scaleX,
                                 box.height * unitY * scaleY};
}

std::vector<std::pair<std::string, std::string>>
formatPerformanceRows(const metadata::PerformanceMeasurementsT &measurements) {
    const auto available = [](const double value) { return std::isfinite(value) && value >= 0.0; };
    std::vector<std::pair<std::string, std::string>> rows;
    if (!measurements.pixel_format.empty())
        rows.emplace_back("Pixel format", measurements.pixel_format);
    for (const auto &timing : measurements.timings) {
        if (!timing)
            continue;
        const auto label = !timing->name.empty()       ? timing->name
                           : !timing->scope_id.empty() ? timing->scope_id
                                                       : "Unnamed scope";
        auto value = timing->mean_ms && available(*timing->mean_ms)
                         ? std::format("{:7.2f}ms", *timing->mean_ms)
                         : "  --.--ms";
        if (timing->p95_ms && available(*timing->p95_ms))
            value += std::format(" (p95: {:.2f}ms)", *timing->p95_ms);
        rows.emplace_back(label, std::move(value));
    }
    if (measurements.frame_rate) {
        const auto &fps = measurements.frame_rate->fps;
        rows.emplace_back(
            "Pipeline", fps && available(*fps) ? std::format("{:6.1f} FPS", *fps) : "   --.- FPS");
    }
    return rows;
}

std::vector<uint8_t> serialize(const FrameResults &frameResults) {
    auto packet = frameResults.serialize();
    return {packet.data(), packet.data() + packet.size()};
}

} // namespace open_perception_kit
