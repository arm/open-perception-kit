/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include "open_perception_kit.h"
#include "opk/Bitmap.h"

#include <cstdint>
#include <memory>
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

std::unique_ptr<metadata::BitmapDataT> makeBitmapData(const opk::Bitmap &bitmap);

void appendPerformanceOverlay(FrameResults &frameResults, const std::vector<std::string> &lines);

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
