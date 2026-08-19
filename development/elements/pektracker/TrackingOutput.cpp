/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "TrackingOutput.h"

#include <cassert>
#include <fmt/core.h>
#include <memory>
#include <utility>

namespace pek::tracker::trackingoutput {

namespace {

constexpr const char *TRACKER_MODEL = "Tracker";
constexpr const char *TRACKER_ENGINE = "std";
constexpr const char *TRACKER_COMPONENT = "gstreamer/pektracker";
constexpr const char *PREDICTION_TAG = "tracking-prediction";
constexpr const char *TRACE_TAG = "tracking";

uint64_t idOf(const perception::metadata::BoxDetectionT &detection) {
    return detection.object ? detection.object->id : 0U;
}

uint64_t parentIdOf(const perception::metadata::BoxDetectionT &detection) {
    return detection.object ? detection.object->parent_id : 0U;
}

const perception::metadata::BoxDetectionT &detectionAt(const DetectionBatch &detections,
                                                       DetectionIndex detectionIndex) {
    assert(detectionIndex < detections.size());
    assert(detections[detectionIndex] != nullptr);
    return *detections[detectionIndex];
}

std::unique_ptr<perception::metadata::ObjectMetaT>
copyObjectMeta(const perception::metadata::ObjectMetaT *object) {
    return object ? std::make_unique<perception::metadata::ObjectMetaT>(*object) : nullptr;
}

std::unique_ptr<perception::metadata::BoundingBoxT>
copyBoundingBox(const perception::metadata::BoundingBoxT *box) {
    return box ? std::make_unique<perception::metadata::BoundingBoxT>(*box) : nullptr;
}

std::string
formatTrackText(const std::string &existingText, TrackId trackId, const std::string &diagnostic) {
    if (existingText.empty()) {
        return fmt::format("ID:{} {}", trackId, diagnostic);
    }
    return fmt::format("{} [ID:{} {}]", existingText, trackId, diagnostic);
}

void appendTrackTextIfEnabled(perception::metadata::BoxDetectionT &detection,
                              TrackId trackId,
                              const TrackState &track,
                              const Config &config) {
    if (!config.appendIdentityIdToText) {
        return;
    }

    detection.text = formatTrackText(detection.text, trackId, track.lastMatchDiagnostic);
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

std::unique_ptr<perception::metadata::LayerInfoT>
makeTrackerLayerInfo(const Config &config, const char *tags, const char *contentType) {
    const std::string producerInstanceId =
        config.producerInstanceId.empty() ? "pektracker" : config.producerInstanceId;
    const auto producer = perception::makeProducerInfo(
        config.inferId + "/" + producerInstanceId, TRACKER_COMPONENT, "Tracker");
    return perception::makeLayerInfo({.model = TRACKER_MODEL,
                                      .inferElementId = config.inferId,
                                      .contentType = contentType,
                                      .engine = TRACKER_ENGINE,
                                      .tags = tags,
                                      .producer = producer.get()});
}

std::unique_ptr<perception::metadata::ObjectTrackT>
makeTrackPayload(const perception::metadata::BoxDetectionT &detection,
                 TrackId trackId,
                 const TrackState &track,
                 const Config &config,
                 bool predictedOnly) {
    auto renderedDetection = detection;
    appendTrackTextIfEnabled(renderedDetection, trackId, track, config);

    const uint64_t sourceId = idOf(renderedDetection);

    auto item = std::make_unique<perception::metadata::ObjectTrackT>();
    item->object = perception::makeObjectMeta(0U, sourceId);
    item->source_id = sourceId;
    item->track_id = trackId;
    item->box = copyBoundingBox(renderedDetection.box.get());
    item->confidence = renderedDetection.confidence;
    item->class_id = renderedDetection.class_id;
    item->text = renderedDetection.text;
    item->diagnostic = track.lastMatchDiagnostic;
    item->predicted_only = predictedOnly;
    return item;
}

perception::metadata::BoxDetectionT detectionForAssignedTrack(const DetectionBatch &detections,
                                                              DetectionIndex detectionIndex,
                                                              const TrackState &track) {
    const auto &currentDetection = detectionAt(detections, detectionIndex);
    auto detection = track.lastDetection;
    detection.object = copyObjectMeta(currentDetection.object.get());
    detection.text = currentDetection.text;
    return detection;
}

} // namespace

void appendTrackingPayloads(perception::FrameResults &frameResults,
                            const DetectionBatch &detections,
                            const ActiveTrackMap &activeTracks,
                            const Config &config,
                            const TrackingResult &trackingResult) {
    perception::metadata::ObjectTracksT tracksPayload;
    tracksPayload.layer = makeTrackerLayerInfo(config, PREDICTION_TAG, config.contentType.c_str());

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
                             false));
    }

    for (const TrackId trackId : trackingResult.predictedOnlyTrackIds) {
        const auto *track = findConfirmedTrack(trackId, activeTracks, config);
        if (track == nullptr) {
            continue;
        }

        tracksPayload.tracks.push_back(
            makeTrackPayload(track->lastDetection, trackId, *track, config, true));
    }

    if (!tracksPayload.tracks.empty()) {
        frameResults.add(std::move(tracksPayload));
    }

    if (!config.emitTrace) {
        return;
    }

    perception::metadata::TrackTracesT tracesPayload;
    tracesPayload.layer = makeTrackerLayerInfo(config, TRACE_TAG, "trackTrace");

    for (const auto &[trackId, track] : activeTracks) {
        if (!shouldEmitTrace(track, config)) {
            continue;
        }

        auto trace = std::make_unique<perception::metadata::TrackTraceT>();
        trace->object = perception::makeObjectMeta(0U, parentIdOf(track.lastDetection));
        trace->track_id = trackId;
        for (const auto &point : track.traceHistoryPoints) {
            trace->points.push_back(std::make_unique<perception::metadata::Point2fT>(point));
        }
        tracesPayload.traces.push_back(std::move(trace));
    }

    if (!tracesPayload.traces.empty()) {
        frameResults.add(std::move(tracesPayload));
    }
}

} // namespace pek::tracker::trackingoutput
