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

#include "TrackingOutput.h"

#include <cassert>
#include <cmath>
#include <memory>
#include <utility>

namespace opk::tracker::trackingoutput {

namespace {

constexpr const char *TRACKER_MODEL = "Tracker";
constexpr const char *TRACKER_ENGINE = "std";
constexpr const char *TRACKER_COMPONENT = "gstreamer/opktracker";

uint64_t idOf(const open_perception_kit::metadata::BoxDetectionT &detection) {
    return detection.object ? detection.object->id : 0U;
}

const open_perception_kit::metadata::BoxDetectionT &detectionAt(const DetectionBatch &detections,
                                                                DetectionIndex detectionIndex) {
    assert(detectionIndex < detections.size());
    assert(detections[detectionIndex] != nullptr);
    return *detections[detectionIndex];
}

std::unique_ptr<open_perception_kit::metadata::ObjectMetaT>
copyObjectMeta(const open_perception_kit::metadata::ObjectMetaT *object) {
    return object ? std::make_unique<open_perception_kit::metadata::ObjectMetaT>(*object) : nullptr;
}

std::unique_ptr<open_perception_kit::metadata::BoundingBoxT>
copyBoundingBox(const open_perception_kit::metadata::BoundingBoxT *box) {
    return box ? std::make_unique<open_perception_kit::metadata::BoundingBoxT>(*box) : nullptr;
}

TrackId lookupAssignedTrackId(DetectionIndex detectionIndex,
                              const TrackingResult &trackingResult,
                              TrackId defaultId = 0) {
    const auto assignmentIt = trackingResult.detectionTrackAssignments.find(detectionIndex);
    if (assignmentIt == trackingResult.detectionTrackAssignments.end()) {
        return defaultId;
    }
    return assignmentIt->second;
}

const TrackState *
findConfirmedTrack(TrackId trackId, const ActiveTrackMap &activeTracks, const Config &config) {
    if (trackId == 0) {
        return nullptr;
    }

    const auto trackIt = activeTracks.find(trackId);
    if (trackIt == activeTracks.end()) {
        return nullptr;
    }

    if (trackIt->second.hitStreak < config.minHitsToConfirm) {
        return nullptr;
    }

    return &trackIt->second;
}

bool shouldEmitTrace(const TrackState &track, const Config &config) {
    return track.hitStreak >= config.minHitsToConfirm && track.traceHistoryPoints.size() >= 2;
}

std::unique_ptr<open_perception_kit::metadata::LayerInfoT>
makeTrackerLayerInfo(const Config &config) {
    const std::string producerInstanceId =
        config.producerInstanceId.empty() ? "opktracker" : config.producerInstanceId;
    const auto producer = open_perception_kit::makeProducerInfo(
        config.inferId + "/" + producerInstanceId, TRACKER_COMPONENT, "Tracker");
    return open_perception_kit::makeLayerInfo({.model = TRACKER_MODEL,
                                               .inferElementId = config.inferId,
                                               .engine = TRACKER_ENGINE,
                                               .producer = producer.get()});
}

std::unique_ptr<open_perception_kit::metadata::TrackIdentityT>
makeIdentity(const Config &config, const TrackingResult &result, const TrackId id) {
    auto identity = std::make_unique<open_perception_kit::metadata::TrackIdentityT>();
    identity->tracker_id =
        config.inferId + "/" +
        (config.producerInstanceId.empty() ? "opktracker" : config.producerInstanceId);
    identity->session_id = result.sessionId;
    identity->track_id = id;
    return identity;
}

std::unique_ptr<open_perception_kit::metadata::ObjectTrackT>
makeTrackPayload(const open_perception_kit::metadata::BoxDetectionT &detection,
                 TrackId trackId,
                 const TrackState &track,
                 const Config &config,
                 const TrackingResult &result,
                 bool predictedOnly) {
    const uint64_t sourceId = idOf(detection);

    auto item = std::make_unique<open_perception_kit::metadata::ObjectTrackT>();
    item->object = open_perception_kit::makeObjectMeta(0U, sourceId);
    if (sourceId != 0 && track.lastObservationFrameIndex != 0) {
        item->source_id = sourceId;
        item->source_frame_index = track.lastObservationFrameIndex;
    }
    item->identity = makeIdentity(config, result, trackId);
    item->box = copyBoundingBox(detection.box.get());
    if (std::isfinite(detection.confidence) && detection.confidence >= 0 &&
        detection.confidence <= 1)
        item->source_confidence = detection.confidence;
    item->class_id = detection.class_id;
    item->text = detection.text;
    if (!predictedOnly)
        item->association = std::make_unique<open_perception_kit::metadata::TrackAssociationT>(
            track.lastAssociation);
    item->sample = std::make_unique<open_perception_kit::metadata::TrackingSampleT>(result.sample);
    item->sample->state = predictedOnly
                              ? open_perception_kit::metadata::TrackObservationState::Predicted
                              : open_perception_kit::metadata::TrackObservationState::Observed;
    return item;
}

open_perception_kit::metadata::BoxDetectionT detectionForAssignedTrack(
    const DetectionBatch &detections, DetectionIndex detectionIndex, const TrackState &track) {
    const auto &currentDetection = detectionAt(detections, detectionIndex);
    auto detection = track.lastDetection;
    detection.object = copyObjectMeta(currentDetection.object.get());
    detection.text = currentDetection.text;
    return detection;
}

} // namespace

void appendTrackingPayloads(open_perception_kit::FrameResults &frameResults,
                            const DetectionBatch &detections,
                            const ActiveTrackMap &activeTracks,
                            const Config &config,
                            const TrackingResult &trackingResult) {
    open_perception_kit::metadata::ObjectTracksT tracksPayload;
    tracksPayload.layer = makeTrackerLayerInfo(config);
    tracksPayload.display_identity = config.appendIdentityIdToText;

    for (DetectionIndex detectionIndex = 0; detectionIndex < detections.size(); ++detectionIndex) {
        const TrackId trackId = lookupAssignedTrackId(detectionIndex, trackingResult);
        const auto *track = findConfirmedTrack(trackId, activeTracks, config);
        if (track == nullptr) {
            continue;
        }

        tracksPayload.tracks.push_back(
            makeTrackPayload(detectionForAssignedTrack(detections, detectionIndex, *track),
                             trackId,
                             *track,
                             config,
                             trackingResult,
                             false));
    }

    for (const TrackId trackId : trackingResult.predictedOnlyTrackIds) {
        const auto *track = findConfirmedTrack(trackId, activeTracks, config);
        if (track == nullptr) {
            continue;
        }

        tracksPayload.tracks.push_back(
            makeTrackPayload(track->lastDetection, trackId, *track, config, trackingResult, true));
    }

    frameResults.add(std::move(tracksPayload));

    if (!config.emitTrace) {
        return;
    }

    open_perception_kit::metadata::TrackTracesT tracesPayload;
    tracesPayload.layer = makeTrackerLayerInfo(config);

    for (const auto &[trackId, track] : activeTracks) {
        if (!shouldEmitTrace(track, config)) {
            continue;
        }

        auto trace = std::make_unique<open_perception_kit::metadata::TrackTraceT>();
        trace->object = open_perception_kit::makeObjectMeta();
        trace->identity = makeIdentity(config, trackingResult, trackId);
        if (track.lastDetection.box && track.lastDetection.box->space)
            trace->space = std::make_unique<open_perception_kit::metadata::ImageSpaceT>(
                *track.lastDetection.box->space);
        for (const auto &point : track.traceHistoryPoints) {
            trace->points.push_back(
                std::make_unique<open_perception_kit::metadata::TrackTracePointT>(point));
        }
        tracesPayload.traces.push_back(std::move(trace));
    }

    frameResults.add(std::move(tracesPayload));
}

} // namespace opk::tracker::trackingoutput
