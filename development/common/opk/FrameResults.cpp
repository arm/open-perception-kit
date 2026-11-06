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

#include <cmath>
#include <format>
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
    info->content_type = descriptor.contentType;
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

std::unique_ptr<metadata::BitmapDataT> makeBitmapData(const opk::Bitmap &bitmap) {
    auto data = std::make_unique<metadata::BitmapDataT>();
    data->width = static_cast<uint32_t>(bitmap.getWidth());
    data->height = static_cast<uint32_t>(bitmap.getHeight());
    data->value_type = bitmap.getType() == opk::Bitmap::Type::Uint32 ? "Uint32" : "Uint8";
    auto pixels = bitmap.getPixels();
    data->pixels.assign(pixels.begin(), pixels.end());
    return data;
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
