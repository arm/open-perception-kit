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
#pragma once
#include "opk/FrameResults.h"
#include <cmath>
#include <format>

namespace opk::tracking {
using namespace open_perception_kit::metadata;

inline bool validIdentity(const TrackIdentityT *const value) noexcept {
    return value && !value->tracker_id.empty() && !value->session_id.empty() &&
           value->track_id != 0;
}
inline bool validSample(const TrackingSampleT *const value) noexcept {
    return value && value->frame_index > 0 && std::isfinite(value->elapsed_ms) &&
           value->elapsed_ms >= 0 &&
           (value->state == TrackObservationState::Observed ||
            value->state == TrackObservationState::Predicted) &&
           (value->time_source == TrackingTimeSource::MediaDelta ||
            value->time_source == TrackingTimeSource::FallbackDelta);
}
inline bool validAssociation(const TrackAssociationT *const value) noexcept {
    if (!value)
        return true;
    if (value->kind == TrackAssociationKind::Unspecified ||
        value->kind == TrackAssociationKind::New)
        return !value->score;
    if (!value->score || !std::isfinite(*value->score))
        return false;
    if (value->kind == TrackAssociationKind::IoUScore)
        return *value->score >= 0 && *value->score <= 1;
    return (value->kind == TrackAssociationKind::CosineScore ||
            value->kind == TrackAssociationKind::Reacquired) &&
           *value->score >= -1 && *value->score <= 1;
}
inline bool validTrack(const ObjectTrackT &value) noexcept {
    if (!validIdentity(value.identity.get()) || !validSample(value.sample.get()) ||
        !validAssociation(value.association.get()) || !value.box || !value.box->space ||
        !open_perception_kit::projectBoundingBox(
            *value.box, value.box->space->width, value.box->space->height))
        return false;
    if (value.source_confidence && (!std::isfinite(*value.source_confidence) ||
                                    *value.source_confidence < 0 || *value.source_confidence > 1))
        return false;
    if (value.sample->state == TrackObservationState::Predicted && value.association)
        return false;
    if (!value.source_id)
        return !value.source_frame_index;
    if (!value.source_frame_index)
        return false;
    return *value.source_id > 0 && *value.source_frame_index > 0 &&
           (value.sample->state == TrackObservationState::Observed
                ? *value.source_frame_index == value.sample->frame_index
                : *value.source_frame_index < value.sample->frame_index);
}
inline bool validTrace(const TrackTraceT &trace) noexcept {
    if (!validIdentity(trace.identity.get()) ||
        !open_perception_kit::validImageSpace(trace.space.get()))
        return false;
    const TrackingSampleT *previous = nullptr;
    for (const auto &point : trace.points) {
        if (!point || !std::isfinite(point->x) || !std::isfinite(point->y) ||
            !validSample(point->sample.get()))
            return false;
        if (previous && (point->sample->frame_index <= previous->frame_index ||
                         point->sample->elapsed_ms <= previous->elapsed_ms))
            return false;
        previous = point->sample.get();
    }
    return true;
}
inline std::string diagnostic(const ObjectTrackT &value) {
    if (value.sample && value.sample->state == TrackObservationState::Predicted)
        return "PRED";
    const auto *const association = value.association.get();
    if (!association || !validAssociation(association))
        return "IOU:N/A";
    if (association->kind == TrackAssociationKind::New)
        return "NEW";
    if (!association->score)
        return "IOU:N/A";
    const auto prefix = association->kind == TrackAssociationKind::IoUScore     ? "IOU"
                        : association->kind == TrackAssociationKind::Reacquired ? "REID-R"
                                                                                : "REID";
    return std::format("{}:{:.2f}", prefix, *association->score);
}
inline std::string label(const ObjectTrackT &value, const bool displayIdentity) {
    if (!displayIdentity || !validIdentity(value.identity.get()))
        return value.text;
    const auto suffix = std::format("ID:{} {}", value.identity->track_id, diagnostic(value));
    return value.text.empty() ? suffix : std::format("{} [{}]", value.text, suffix);
}
} // namespace opk::tracking
