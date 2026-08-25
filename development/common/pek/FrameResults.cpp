/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "pek/FrameResults.h"

#include "pek/Tools.h"

#include <utility>

namespace perception {

std::unique_ptr<metadata::ObjectMetaT>
makeObjectMeta(uint64_t id, uint64_t parentId, uint64_t creationTsNs) {
    auto object = std::make_unique<metadata::ObjectMetaT>();
    object->id = id == 0U ? pek::nextObjectId() : id;
    object->parent_id = parentId;
    object->creation_ts_ns = creationTsNs == 0U ? pek::Time::utcNano() : creationTsNs;
    return object;
}

std::unique_ptr<metadata::LayerInfoT> makeLayerInfo(std::string_view model,
                                                    std::string_view inferElementId,
                                                    std::string_view contentType,
                                                    std::string_view engine,
                                                    std::string_view tags,
                                                    std::string_view labelFamily,
                                                    std::string_view compositingMode) {
    auto info = std::make_unique<metadata::LayerInfoT>();
    info->engine = engine;
    info->model = model;
    info->tags = tags;
    info->infer_element_id = inferElementId;
    info->label_family = labelFamily;
    info->content_type = contentType;
    info->compositing_mode = compositingMode;
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

std::unique_ptr<metadata::BitmapDataT> makeBitmapData(const pek::Bitmap &bitmap) {
    auto data = std::make_unique<metadata::BitmapDataT>();
    data->width = static_cast<uint32_t>(bitmap.getWidth());
    data->height = static_cast<uint32_t>(bitmap.getHeight());
    data->value_type = bitmap.getType() == pek::Bitmap::Type::Uint32 ? "Uint32" : "Uint8";
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

} // namespace perception
