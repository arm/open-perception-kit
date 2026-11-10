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

#include "TextDisplay.h"
#include "opk/AudioResults.h"
#include "opk/ClassificationScores.h"
#include "opk/Embedding.h"
#include "opk/FrameResults.h"
#include "opk/Tracking.h"

#include <fmt/core.h>

#include <algorithm>
#include <cmath>
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
    if (!box || !box->space) {
        return "box=-";
    }
    const auto pixels =
        open_perception_kit::projectBoundingBox(*box, box->space->width, box->space->height);
    if (!pixels)
        return "box=unavailable";
    return fmt::format("x={:.1f} y={:.1f} w={:.1f} h={:.1f}",
                       (*pixels)[0],
                       (*pixels)[1],
                       (*pixels)[2],
                       (*pixels)[3]);
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
        for (std::size_t byte = 0; byte < 4; ++byte)
            value |= static_cast<uint32_t>(pixels[offset + byte]) << (8 * byte);
        minValue = std::min(minValue, value);
        maxValue = std::max(maxValue, value);
    }

    return fmt::format("min={} max={}", minValue, maxValue);
}

std::string bitmapRange(const BitmapDataT *bitmap) {
    if (!bitmap) {
        return "bitmap=-";
    }

    if (!open_perception_kit::validBitmapData(bitmap))
        return "values=invalid-bitmap";
    if (bitmap->encoding == open_perception_kit::metadata::BitmapEncoding::Uint32LE) {
        return valueRangeUint32(bitmap->pixels);
    }

    if (bitmap->encoding == open_perception_kit::metadata::BitmapEncoding::Uint8)
        return valueRangeUint8(bitmap->pixels);
    return "values=unsupported-encoding";
}

std::string bitmapLabel(const BitmapDataT *const bitmap) {
    if (!bitmap)
        return "bitmap=unavailable";
    const auto *const space = bitmap->space.get();
    return fmt::format(
        "bitmap={}x{} encoding={} bytes={} frame={} origin=({},{}) extent=({},{}) {}",
        bitmap->width,
        bitmap->height,
        open_perception_kit::metadata::EnumNameBitmapEncoding(bitmap->encoding),
        bitmap->pixels.size(),
        space ? space->frame_id : 0,
        space ? space->origin_x : 0,
        space ? space->origin_y : 0,
        space ? space->extent_width : 0,
        space ? space->extent_height : 0,
        bitmapRange(bitmap));
}

} // namespace

void TextDisplay::appendLines(std::vector<std::string> &lines,
                              const open_perception_kit::metadata::FrameContextT &payload) {
    lines.push_back(fmt::format("FrameContext: {}", layerLabel(payload.layer.get())));
    if (payload.video) {
        lines.push_back(fmt::format("  video: {} original={}x{} pts_ns={}",
                                    objectLabel(payload.video->object.get()),
                                    payload.video->original_width,
                                    payload.video->original_height,
                                    payload.video->presentation_ts_ns
                                        ? std::to_string(*payload.video->presentation_ts_ns)
                                        : "unavailable"));
    }
    if (payload.audio) {
        lines.push_back(fmt::format("  audio: {} channels={} frequency={} samples={} pts_ns={}",
                                    objectLabel(payload.audio->object.get()),
                                    payload.audio->original_channels,
                                    payload.audio->original_frequency,
                                    payload.audio->original_sample_count,
                                    payload.audio->presentation_ts_ns
                                        ? std::to_string(*payload.audio->presentation_ts_ns)
                                        : "unavailable"));
    }
}

void TextDisplay::appendLines(
    std::vector<std::string> &lines,
    const open_perception_kit::metadata::VoiceActivityDetectionsT &payload) {
    lines.push_back(fmt::format("VoiceActivityDetections: {} count={}",
                                layerLabel(payload.layer.get()),
                                payload.detections.size()));
    for (const auto &value : payload.detections) {
        if (!value || !value->speech_active || !value->speech_probability ||
            !opk::audio_results::valid(*value)) {
            lines.emplace_back("  VAD: unavailable");
            continue;
        }
        lines.push_back(fmt::format(
            "  VAD: active={} probability={:.3f} samples=[{},{}) pts=[{},{}) discontinuity={}",
            *value->speech_active,
            *value->speech_probability,
            value->window_start_sample,
            value->window_end_sample,
            value->window_start_ts_ns ? std::to_string(*value->window_start_ts_ns) : "unavailable",
            value->window_end_ts_ns ? std::to_string(*value->window_end_ts_ns) : "unavailable",
            value->discontinuity));
    }
}

void TextDisplay::appendLines(std::vector<std::string> &lines,
                              const open_perception_kit::metadata::TurnDetectionsT &payload) {
    lines.push_back(fmt::format(
        "TurnDetections: {} count={}", layerLabel(payload.layer.get()), payload.detections.size()));
    for (const auto &value : payload.detections) {
        if (!value || !value->turn_complete || !value->turn_probability ||
            !opk::audio_results::valid(*value)) {
            lines.emplace_back("  Turn: unavailable");
            continue;
        }
        lines.push_back(fmt::format(
            "  Turn: complete={} probability={:.3f} samples=[{},{}) pts=[{},{}) discontinuity={}",
            *value->turn_complete,
            *value->turn_probability,
            value->window_start_sample,
            value->window_end_sample,
            value->window_start_ts_ns ? std::to_string(*value->window_start_ts_ns) : "unavailable",
            value->window_end_ts_ns ? std::to_string(*value->window_end_ts_ns) : "unavailable",
            value->discontinuity));
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

        lines.push_back(
            fmt::format("  box #{}: {} label={} class={} conf={:.3f} {}",
                        index,
                        objectLabel(detection->object.get()),
                        textOr(detection->text),
                        detection->class_id ? std::to_string(*detection->class_id) : "unspecified",
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

        lines.push_back(fmt::format(
            "  track #{}: {} source={} source_frame={} tracker={} session={} track={} label={} "
            "class={} "
            "source_conf={} state={} frame={} elapsed_ms={} valid={} {} display={}",
            index,
            objectLabel(track->object.get()),
            track->source_id ? std::to_string(*track->source_id) : "unavailable",
            track->source_frame_index ? std::to_string(*track->source_frame_index) : "unavailable",
            track->identity ? track->identity->tracker_id : "",
            track->identity ? track->identity->session_id : "",
            track->identity ? track->identity->track_id : 0,
            textOr(track->text),
            track->class_id ? std::to_string(*track->class_id) : "unspecified",
            track->source_confidence ? fmt::format("{:.3f}", *track->source_confidence)
                                     : "unavailable",
            track->sample
                ? open_perception_kit::metadata::EnumNameTrackObservationState(track->sample->state)
                : "Unspecified",
            track->sample ? track->sample->frame_index : 0,
            track->sample ? track->sample->elapsed_ms : 0,
            opk::tracking::validTrack(*track),
            boxLabel(track->box.get()),
            opk::tracking::label(*track, payload.display_identity)));
    }
}

void TextDisplay::appendLines(std::vector<std::string> &lines,
                              const open_perception_kit::metadata::ClassificationsT &payload) {
    lines.push_back(fmt::format("Classifications: {} classifications={}",
                                layerLabel(payload.layer.get()),
                                payload.classifications.size()));
    std::size_t index = 0U;
    for (const auto &classification : payload.classifications) {
        ++index;
        if (!classification) {
            lines.push_back(fmt::format("  classification #{}: <null>", index));
            continue;
        }

        lines.push_back(fmt::format(
            "  classification #{}: {} candidates={} label_set={} kind={} subject={}",
            index,
            objectLabel(classification->object.get()),
            classification->candidates.size(),
            textOr(classification->label_set),
            open_perception_kit::metadata::EnumNameClassificationKind(classification->kind),
            classification->subject_id ? std::to_string(*classification->subject_id)
                                       : "unspecified"));
        std::size_t candidateIndex = 0U;
        for (const auto &candidate : classification->candidates) {
            ++candidateIndex;
            if (!candidate || !opk::classification::validCandidate(*classification, *candidate)) {
                lines.push_back(fmt::format("    candidate #{}: unavailable", candidateIndex));
                continue;
            }

            lines.push_back(fmt::format(
                "    candidate #{}: label={} class={} conf={}",
                candidateIndex,
                textOr(candidate->text),
                candidate->class_id ? std::to_string(*candidate->class_id) : "unspecified",
                candidate->confidence ? fmt::format("{:.3f}", *candidate->confidence)
                                      : "unavailable"));
        }
    }
}

void TextDisplay::appendLines(std::vector<std::string> &lines,
                              const open_perception_kit::metadata::CameraContactsT &payload) {
    lines.push_back(fmt::format(
        "CameraContacts: {} count={}", layerLabel(payload.layer.get()), payload.contacts.size()));
    for (const auto &contact : payload.contacts) {
        if (!contact || !contact->result ||
            (contact->result->confidence &&
             !opk::classification::validProbability(*contact->result->confidence))) {
            lines.emplace_back("  camera-contact: unavailable");
            continue;
        }
        lines.push_back(fmt::format(
            "  camera-contact: {} outcome={} subject={} model_class_index={} conf={}",
            objectLabel(contact->object.get()),
            contact->result->in_contact ? "contact" : "no contact",
            contact->subject_id ? std::to_string(*contact->subject_id) : "unspecified",
            contact->result->model_class_index ? std::to_string(*contact->result->model_class_index)
                                               : "unspecified",
            contact->result->confidence ? fmt::format("{:.3f}", *contact->result->confidence)
                                        : "unavailable"));
    }
}

void TextDisplay::appendLines(std::vector<std::string> &lines,
                              const open_perception_kit::metadata::PersonPresencesT &payload) {
    lines.push_back(fmt::format(
        "PersonPresences: {} count={}", layerLabel(payload.layer.get()), payload.presences.size()));
    std::size_t index = 0U;
    for (const auto &presence : payload.presences) {
        ++index;
        if (!presence) {
            lines.push_back(fmt::format("  person-presence #{}: <null>", index));
            continue;
        }

        if (!presence->result || !opk::classification::validPresenceResult(*presence->result)) {
            lines.push_back(fmt::format("  person-presence #{}: {} unavailable",
                                        index,
                                        objectLabel(presence->object.get())));
            continue;
        }
        const auto &result = *presence->result;
        lines.push_back(
            fmt::format("  person-presence #{}: {} outcome={} raw_yes={} raw_no={}",
                        index,
                        objectLabel(presence->object.get()),
                        result.person_present ? "person" : "no person",
                        result.yes_score ? fmt::format("{:.3f}", *result.yes_score) : "unavailable",
                        result.no_score ? fmt::format("{:.3f}", *result.no_score) : "unavailable"));
    }
}

void TextDisplay::appendLines(std::vector<std::string> &lines,
                              const open_perception_kit::metadata::GazeMeasurementsT &payload) {
    lines.push_back(fmt::format("GazeMeasurements: {} count={}",
                                layerLabel(payload.layer.get()),
                                payload.measurements.size()));
    std::size_t index = 0U;
    for (const auto &gaze : payload.measurements) {
        ++index;
        if (!gaze || !gaze->angles || !std::isfinite(gaze->angles->yaw_degrees) ||
            !std::isfinite(gaze->angles->pitch_degrees) ||
            (gaze->confidence && !opk::classification::validProbability(*gaze->confidence))) {
            lines.push_back(fmt::format("  gaze #{}: unavailable", index));
            continue;
        }

        lines.push_back(
            fmt::format("  gaze #{}: {} subject={} conf={} yaw_degrees={:.3f} pitch_degrees={:.3f}",
                        index,
                        objectLabel(gaze->object.get()),
                        gaze->subject_id ? std::to_string(*gaze->subject_id) : "unspecified",
                        gaze->confidence ? fmt::format("{:.3f}", *gaze->confidence) : "unavailable",
                        gaze->angles->yaw_degrees,
                        gaze->angles->pitch_degrees));
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

        lines.push_back(
            fmt::format("  segmentation #{}: {} meaning={} valid={} {}",
                        index,
                        objectLabel(mask->object.get()),
                        open_perception_kit::metadata::EnumNameOpacityMeaning(mask->meaning),
                        open_perception_kit::validMask(*mask),
                        bitmapLabel(mask->bitmap.get())));
    }
}

void TextDisplay::appendLines(
    std::vector<std::string> &lines,
    const open_perception_kit::metadata::SemanticSegmentationMasksT &payload) {
    lines.push_back(fmt::format("SemanticSegmentationMasks: {} count={}",
                                layerLabel(payload.layer.get()),
                                payload.masks.size()));
    for (const auto &mask : payload.masks) {
        if (!mask) {
            lines.emplace_back("  semantic: unavailable");
            continue;
        }
        lines.push_back(fmt::format("  semantic: {} label_set={} void={} valid={} {}",
                                    objectLabel(mask->object.get()),
                                    mask->label_set,
                                    mask->void_value ? std::to_string(*mask->void_value) : "none",
                                    open_perception_kit::validMask(*mask),
                                    bitmapLabel(mask->bitmap.get())));
    }
}

void TextDisplay::appendLines(
    std::vector<std::string> &lines,
    const open_perception_kit::metadata::InstanceSegmentationMasksT &payload) {
    lines.push_back(fmt::format("InstanceSegmentationMasks: {} count={}",
                                layerLabel(payload.layer.get()),
                                payload.masks.size()));
    for (const auto &mask : payload.masks) {
        if (!mask) {
            lines.emplace_back("  instance: unavailable");
            continue;
        }
        lines.push_back(
            fmt::format("  instance: {} label_set={} class_id={} confidence={} valid={} {} {}",
                        objectLabel(mask->object.get()),
                        mask->label_set,
                        mask->class_id ? std::to_string(*mask->class_id) : "unavailable",
                        mask->confidence ? fmt::format("{:.3f}", *mask->confidence) : "unavailable",
                        open_perception_kit::validMask(*mask),
                        boxLabel(mask->box.get()),
                        bitmapLabel(mask->bitmap.get())));
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

        lines.push_back(fmt::format(
            "  embedding #{}: {} subject={} space={} dimensions={} metric={} usable={} values={}",
            index,
            objectLabel(embedding->object.get()),
            embedding->subject_id ? std::to_string(*embedding->subject_id) : "unspecified",
            embedding->space ? embedding->space->id : "unspecified",
            embedding->space ? embedding->space->dimensions : 0,
            embedding->space
                ? open_perception_kit::metadata::EnumNameEmbeddingMetric(embedding->space->metric)
                : "Unspecified",
            opk::embedding::valid(*embedding),
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

        lines.push_back(
            fmt::format("  trace #{}: {} tracker={} session={} track={} points={} valid={}",
                        index,
                        objectLabel(trace->object.get()),
                        trace->identity ? trace->identity->tracker_id : "",
                        trace->identity ? trace->identity->session_id : "",
                        trace->identity ? trace->identity->track_id : 0,
                        trace->points.size(),
                        opk::tracking::validTrace(*trace)));
    }
}

void TextDisplay::appendLines(
    std::vector<std::string> &lines,
    const open_perception_kit::metadata::PerformanceMeasurementsT &payload) {
    const auto rows = open_perception_kit::formatPerformanceRows(payload);
    // Keep the familiar diagnostic presentation while consuming typed data.
    lines.push_back(fmt::format("PerformanceOverlay: lines={}", rows.size()));
    std::size_t index = 0U;
    for (const auto &[name, value] : rows) {
        ++index;
        lines.push_back(fmt::format("  perf #{}: {:<24}: {}", index, name, value));
    }
}
