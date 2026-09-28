/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

#include "TextDisplay.h"

#include <fmt/core.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

using open_perception_kit::metadata::BitmapDataT;
using open_perception_kit::metadata::BoundingBoxT;
using open_perception_kit::metadata::LayerInfoT;
using open_perception_kit::metadata::ObjectMetaT;

std::string textOr(std::string_view value, std::string_view fallback = "-") {
    return value.empty() ? std::string(fallback) : std::string(value);
}

std::string layerLabel(const LayerInfoT *layer) {
    if (!layer) {
        return "content=- model=- infer=-";
    }

    return fmt::format("content={} model={} infer={}",
                       textOr(layer->content_type),
                       textOr(layer->model),
                       textOr(layer->infer_element_id));
}

std::string objectLabel(const ObjectMetaT *object) {
    if (!object) {
        return "id=- parent=-";
    }

    return fmt::format("id={} parent={}", object->id, object->parent_id);
}

std::string boxLabel(const BoundingBoxT *box) {
    if (!box) {
        return "box=-";
    }

    return fmt::format(
        "x={:.1f} y={:.1f} w={:.1f} h={:.1f}", box->x, box->y, box->width, box->height);
}

std::string valueRangeUint8(const std::vector<std::uint8_t> &pixels) {
    if (pixels.empty()) {
        return "values=empty";
    }

    const auto [minIt, maxIt] = std::minmax_element(pixels.begin(), pixels.end());
    return fmt::format(
        "min={} max={}", static_cast<unsigned>(*minIt), static_cast<unsigned>(*maxIt));
}

std::string valueRangeUint32(const std::vector<std::uint8_t> &pixels) {
    if (pixels.empty()) {
        return "values=empty";
    }
    if (pixels.size() % sizeof(std::uint32_t) != 0U) {
        return fmt::format("values=invalid-uint32-bytes({})", pixels.size());
    }

    std::uint32_t minValue = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t maxValue = std::numeric_limits<std::uint32_t>::lowest();
    for (std::size_t offset = 0; offset < pixels.size(); offset += sizeof(std::uint32_t)) {
        std::uint32_t value = 0U;
        std::memcpy(&value, pixels.data() + offset, sizeof(value));
        minValue = std::min(minValue, value);
        maxValue = std::max(maxValue, value);
    }

    return fmt::format("min={} max={}", minValue, maxValue);
}

std::string bitmapRange(const BitmapDataT *bitmap) {
    if (!bitmap) {
        return "bitmap=-";
    }

    if (bitmap->value_type == "Uint32") {
        return valueRangeUint32(bitmap->pixels);
    }

    return valueRangeUint8(bitmap->pixels);
}

} // namespace

void TextDisplay::appendLines(std::vector<std::string> &lines,
                              const open_perception_kit::metadata::FrameContextT &payload) {
    lines.push_back(fmt::format("FrameContext: {}", layerLabel(payload.layer.get())));
    if (payload.video) {
        lines.push_back(fmt::format(
            "  video: {} original={}x{} crop_lrtb={},{},{},{} letterbox_lrtb={},{},{},{}",
            objectLabel(payload.video->object.get()),
            payload.video->original_width,
            payload.video->original_height,
            payload.video->source_crop_left,
            payload.video->source_crop_right,
            payload.video->source_crop_top,
            payload.video->source_crop_bottom,
            payload.video->letterbox_left,
            payload.video->letterbox_right,
            payload.video->letterbox_top,
            payload.video->letterbox_bottom));
    }
    if (payload.audio) {
        lines.push_back(fmt::format("  audio: {} channels={} frequency={} samples={}",
                                    objectLabel(payload.audio->object.get()),
                                    payload.audio->original_channels,
                                    payload.audio->original_frequency,
                                    payload.audio->original_sample_count));
    }
}

void TextDisplay::appendLines(std::vector<std::string> &lines,
                              const open_perception_kit::metadata::BoxDetectionsT &payload) {
    lines.push_back(fmt::format(
        "BoxDetections: {} count={}", layerLabel(payload.layer.get()), payload.detections.size()));
    std::size_t index = 0U;
    for (const auto &detection : payload.detections) {
        ++index;
        if (!detection) {
            lines.push_back(fmt::format("  box #{}: <null>", index));
            continue;
        }

        lines.push_back(fmt::format("  box #{}: {} label={} class={} conf={:.3f} {}",
                                    index,
                                    objectLabel(detection->object.get()),
                                    textOr(detection->text),
                                    detection->class_id,
                                    detection->confidence,
                                    boxLabel(detection->box.get())));
    }
}

void TextDisplay::appendLines(std::vector<std::string> &lines,
                              const open_perception_kit::metadata::ObjectTracksT &payload) {
    lines.push_back(fmt::format(
        "ObjectTracks: {} count={}", layerLabel(payload.layer.get()), payload.tracks.size()));
    std::size_t index = 0U;
    for (const auto &track : payload.tracks) {
        ++index;
        if (!track) {
            lines.push_back(fmt::format("  track #{}: <null>", index));
            continue;
        }

        lines.push_back(fmt::format("  track #{}: {} source={} track={} label={} class={} "
                                    "conf={:.3f} predicted={} {} diagnostic={}",
                                    index,
                                    objectLabel(track->object.get()),
                                    track->source_id,
                                    track->track_id,
                                    textOr(track->text),
                                    track->class_id,
                                    track->confidence,
                                    track->predicted_only ? "true" : "false",
                                    boxLabel(track->box.get()),
                                    textOr(track->diagnostic)));
    }
}

void TextDisplay::appendLines(std::vector<std::string> &lines,
                              const open_perception_kit::metadata::ClassificationsT &payload) {
    lines.push_back(fmt::format("Classifications: {} classifications={} person_presence={}",
                                layerLabel(payload.layer.get()),
                                payload.classifications.size(),
                                payload.person_presence.size()));
    std::size_t index = 0U;
    for (const auto &classification : payload.classifications) {
        ++index;
        if (!classification) {
            lines.push_back(fmt::format("  classification #{}: <null>", index));
            continue;
        }

        lines.push_back(fmt::format("  classification #{}: {} candidates={}",
                                    index,
                                    objectLabel(classification->object.get()),
                                    classification->candidates.size()));
        std::size_t candidateIndex = 0U;
        for (const auto &candidate : classification->candidates) {
            ++candidateIndex;
            if (!candidate) {
                lines.push_back(fmt::format("    candidate #{}: <null>", candidateIndex));
                continue;
            }

            lines.push_back(fmt::format("    candidate #{}: label={} class={} conf={:.3f} "
                                        "region={:.1f},{:.1f},{:.1f},{:.1f}",
                                        candidateIndex,
                                        textOr(candidate->text),
                                        candidate->class_id,
                                        candidate->confidence,
                                        candidate->x,
                                        candidate->y,
                                        candidate->w,
                                        candidate->h));
        }
    }

    index = 0U;
    for (const auto &presence : payload.person_presence) {
        ++index;
        if (!presence) {
            lines.push_back(fmt::format("  person-presence #{}: <null>", index));
            continue;
        }

        lines.push_back(fmt::format("  person-presence #{}: {} yes={:.3f} no={:.3f}",
                                    index,
                                    objectLabel(presence->object.get()),
                                    presence->yes_confidence,
                                    presence->no_confidence));
    }
}

void TextDisplay::appendLines(std::vector<std::string> &lines,
                              const open_perception_kit::metadata::PoseEstimationsT &payload) {
    lines.push_back(fmt::format(
        "PoseEstimations: {} count={}", layerLabel(payload.layer.get()), payload.poses.size()));
    std::size_t index = 0U;
    for (const auto &pose : payload.poses) {
        ++index;
        if (!pose) {
            lines.push_back(fmt::format("  pose #{}: <null>", index));
            continue;
        }

        lines.push_back(fmt::format("  pose #{}: {} conf={:.3f} yaw={:.3f} pitch={:.3f}",
                                    index,
                                    objectLabel(pose->object.get()),
                                    pose->confidence,
                                    pose->yaw,
                                    pose->pitch));
    }
}

void TextDisplay::appendLines(std::vector<std::string> &lines,
                              const open_perception_kit::metadata::SegmentationMasksT &payload) {
    lines.push_back(fmt::format(
        "SegmentationMasks: {} count={}", layerLabel(payload.layer.get()), payload.masks.size()));
    std::size_t index = 0U;
    for (const auto &mask : payload.masks) {
        ++index;
        if (!mask) {
            lines.push_back(fmt::format("  segmentation #{}: <null>", index));
            continue;
        }

        const auto *bitmap = mask->bitmap.get();
        lines.push_back(fmt::format("  segmentation #{}: {} bitmap={}x{} value_type={} "
                                    "bytes={} {}",
                                    index,
                                    objectLabel(mask->object.get()),
                                    bitmap ? bitmap->width : 0U,
                                    bitmap ? bitmap->height : 0U,
                                    bitmap ? textOr(bitmap->value_type) : "-",
                                    bitmap ? bitmap->pixels.size() : 0U,
                                    bitmapRange(bitmap)));
    }
}

void TextDisplay::appendLines(std::vector<std::string> &lines,
                              const open_perception_kit::metadata::ObjectEmbeddingsT &payload) {
    lines.push_back(fmt::format("ObjectEmbeddings: {} count={}",
                                layerLabel(payload.layer.get()),
                                payload.embeddings.size()));
    std::size_t index = 0U;
    for (const auto &embedding : payload.embeddings) {
        ++index;
        if (!embedding) {
            lines.push_back(fmt::format("  embedding #{}: <null>", index));
            continue;
        }

        lines.push_back(fmt::format("  embedding #{}: {} values={}",
                                    index,
                                    objectLabel(embedding->object.get()),
                                    embedding->values.size()));
    }
}

void TextDisplay::appendLines(std::vector<std::string> &lines,
                              const open_perception_kit::metadata::TrackTracesT &payload) {
    lines.push_back(fmt::format(
        "TrackTraces: {} count={}", layerLabel(payload.layer.get()), payload.traces.size()));
    std::size_t index = 0U;
    for (const auto &trace : payload.traces) {
        ++index;
        if (!trace) {
            lines.push_back(fmt::format("  trace #{}: <null>", index));
            continue;
        }

        lines.push_back(fmt::format("  trace #{}: {} track={} points={}",
                                    index,
                                    objectLabel(trace->object.get()),
                                    trace->track_id,
                                    trace->points.size()));
    }
}

void TextDisplay::appendLines(std::vector<std::string> &lines,
                              const open_perception_kit::metadata::PerformanceOverlayT &payload) {
    lines.push_back(fmt::format("PerformanceOverlay: lines={}", payload.lines.size()));
    std::size_t index = 0U;
    for (const auto &line : payload.lines) {
        ++index;
        lines.push_back(fmt::format("  perf #{}: {}", index, line));
    }
}
