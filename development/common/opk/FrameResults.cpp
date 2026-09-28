/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#include "opk/FrameResults.h"

#include "opk/Tools.h"

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

void appendPerformanceOverlay(FrameResults &frameResults, const std::vector<std::string> &lines) {
    metadata::PerformanceOverlayT overlay;
    overlay.lines = lines;
    frameResults.add(std::move(overlay));
}

std::vector<uint8_t> serialize(const FrameResults &frameResults) {
    auto packet = frameResults.serialize();
    return {packet.data(), packet.data() + packet.size()};
}

} // namespace open_perception_kit
