/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Bitmap.h"
#include "perception.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace perception {

using FrameResults = container::envelope;

std::unique_ptr<metadata::ObjectMetaT>
makeObjectMeta(uint64_t id = 0, uint64_t parentId = 0, uint64_t creationTsNs = 0);

std::unique_ptr<metadata::LayerInfoT> makeLayerInfo(const std::string &model,
                                                    const std::string &inferElementId,
                                                    const std::string &contentType,
                                                    const std::string &engine = "",
                                                    const std::string &tags = "",
                                                    const std::string &labelFamily = "",
                                                    const std::string &compositingMode = "");

std::unique_ptr<metadata::BoundingBoxT>
makeBoundingBox(float x, float y, float width, float height);

std::unique_ptr<metadata::BitmapDataT> makeBitmapData(const pek::Bitmap &bitmap);

void appendPerformanceOverlay(FrameResults &frameResults, const std::vector<std::string> &lines);

template <typename Fn>
void forEachBoxDetectionWithContentType(const FrameResults &frameResults,
                                        const std::string &contentType,
                                        uint64_t id,
                                        Fn &&fn) {
    frameResults.for_each<metadata::BoxDetectionsT>([&](const auto &payload) {
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
    forEachBoxDetectionWithContentType(frameResults, contentType, 0U, fn);
}

std::vector<uint8_t> serialize(const FrameResults &frameResults);

} // namespace perception
