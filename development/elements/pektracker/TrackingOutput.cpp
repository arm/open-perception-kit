/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "TrackingOutput.h"

#include <fmt/core.h>

#include <utility>

namespace pek::tracker::trackingoutput {

namespace {

constexpr const char *TRACKER_MODEL = "Tracker";
constexpr const char *TRACKER_ENGINE = "std";
constexpr const char *PREDICTION_TAG = "tracking-prediction";
constexpr const char *TRACE_TAG = "tracking";

bool isTargetLayer(const pek::Perception::Layer &layer, const Config &config) {
    return layer.contentType == config.contentType;
}

std::string
formatTrackText(const std::string &existingText, TrackId trackId, const std::string &diagnostic) {
    if (existingText.empty()) {
        return fmt::format("ID:{} {}", trackId, diagnostic);
    }
    return fmt::format("{} [ID:{} {}]", existingText, trackId, diagnostic);
}

void appendTrackTextIfEnabled(pek::Perception::Rect &rect,
                              TrackId trackId,
                              const TrackState &track,
                              const Config &config) {
    if (!config.appendIdentityIdToText) {
        return;
    }

    rect.text = formatTrackText(rect.text, trackId, track.lastMatchDiagnostic);
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

const TrackState *findConfirmedTrack(TrackId trackId, const WriterContext &context) {
    if (trackId == 0) {
        return nullptr;
    }

    const auto trackIt = context.activeTracks.find(trackId);
    if (trackIt == context.activeTracks.end()) {
        return nullptr;
    }

    if (trackIt->second.hitStreak < context.config.minHitsToConfirm) {
        return nullptr;
    }

    return &trackIt->second;
}

void applyAssignedTrackToDetection(pek::Perception::Rect &rect,
                                   DetectionIndex detectionIndex,
                                   const WriterContext &context,
                                   const TrackingResult &trackingResult) {
    const TrackId trackId = lookupAssignedTrackId(detectionIndex, trackingResult);
    const auto *track = findConfirmedTrack(trackId, context);
    if (track == nullptr) {
        return;
    }

    rect.x = track->lastDetection.x;
    rect.y = track->lastDetection.y;
    rect.attributes["trackId"] = std::to_string(trackId);
    appendTrackTextIfEnabled(rect, trackId, *track, context.config);
}

bool shouldEmitTrace(const TrackState &track, const Config &config) {
    return track.hitStreak >= config.minHitsToConfirm && track.traceHistoryPoints.size() >= 2;
}

void appendTrackTraceDetection(pek::Perception::Layer &traceLayer,
                               TrackId trackId,
                               const TrackState &track) {
    pek::Perception::TrackTrace trace;
    trace.trackId = trackId;
    trace.parentUuid = track.lastDetection.parentUuid;
    trace.points.assign(track.traceHistoryPoints.begin(), track.traceHistoryPoints.end());
    traceLayer.detections.emplace_back(std::move(trace));
}

pek::Perception::Layer *ensurePredictionOutputLayer(pek::Perception &perception,
                                                    const Config &config) {
    for (auto &layer : perception.layers) {
        if (layer.model == TRACKER_MODEL && layer.tags.find(PREDICTION_TAG) != std::string::npos &&
            isTargetLayer(layer, config)) {
            return &layer;
        }
    }

    pek::Perception::Layer predictedLayer;
    predictedLayer.model = TRACKER_MODEL;
    predictedLayer.engine = TRACKER_ENGINE;
    predictedLayer.tags = PREDICTION_TAG;
    predictedLayer.contentType = config.contentType;
    predictedLayer.inferElementId = config.inferId + "_pl";
    perception.layers.push_back(std::move(predictedLayer));
    return &perception.layers.back();
}

} // namespace

void updateExistingDetectionsWithTrackingResult(const WriterContext &context,
                                                const TrackingResult &trackingResult) {
    DetectionIndex detectionIndex = 0;
    for (auto &layer : context.perception.layers) {
        if (!isTargetLayer(layer, context.config)) {
            continue;
        }

        for (auto &det : layer.detections) {
            auto *rect = std::get_if<pek::Perception::Rect>(&det);
            if (!rect) {
                continue;
            }

            applyAssignedTrackToDetection(*rect, detectionIndex, context, trackingResult);

            detectionIndex++;
        }
    }
}

void appendPredictedDetectionsFromTrackingResult(const WriterContext &context,
                                                 const TrackingResult &trackingResult) {
    auto *predictionLayer = ensurePredictionOutputLayer(context.perception, context.config);

    for (const TrackId trackId : trackingResult.predictedOnlyTrackIds) {
        const auto *track = findConfirmedTrack(trackId, context);
        if (track == nullptr) {
            continue;
        }

        auto predictedRect = track->lastDetection;
        appendTrackTextIfEnabled(predictedRect, trackId, *track, context.config);

        predictionLayer->detections.emplace_back(std::move(predictedRect));
    }
}

void appendTraceLayerForActiveTracks(const WriterContext &context) {
    pek::Perception::Layer traceLayer;
    traceLayer.model = TRACKER_MODEL;
    traceLayer.engine = TRACKER_ENGINE;
    traceLayer.tags = TRACE_TAG;
    traceLayer.contentType = "trackTrace";
    traceLayer.inferElementId = context.config.inferId + "_tl";

    for (const auto &[trackId, track] : context.activeTracks) {
        if (!shouldEmitTrace(track, context.config)) {
            continue;
        }

        appendTrackTraceDetection(traceLayer, trackId, track);
    }

    if (!traceLayer.detections.empty()) {
        context.perception.layers.push_back(std::move(traceLayer));
    }
}

} // namespace pek::tracker::trackingoutput
