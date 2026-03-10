/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "TrackingOutput.h"

#include <fmt/core.h>

namespace amp::tracker::trackingoutput {

namespace {

constexpr const char *TRACKER_MODEL = "Tracker";
constexpr const char *TRACKER_ENGINE = "std";
constexpr const char *PREDICTION_TAG = "tracking-prediction";
constexpr const char *TRACE_TAG = "tracking";

bool isTargetLayer(const amp::Perception::Layer &layer, const Config &config) {
    return layer.contentType == config.contentType;
}

std::string formatIdentityText(const std::string &existingText,
                               IdentityId identityId,
                               const std::string &diagnostic) {
    if (existingText.empty()) {
        return fmt::format("ID:{} {}", identityId, diagnostic);
    }
    return fmt::format("{} [ID:{} {}]", existingText, identityId, diagnostic);
}

void appendIdentityTextIfEnabled(amp::Perception::Rect &rect,
                                 IdentityId identityId,
                                 const Identity &identity,
                                 const Config &config) {
    if (!config.appendIdentityIdToText) {
        return;
    }

    rect.text = formatIdentityText(rect.text, identityId, identity.lastMatchDiagnostic);
}

IdentityId lookupAssignedIdentityId(DetectionIndex detectionIndex,
                                    const TrackingResult &trackingResult,
                                    IdentityId defaultId = 0) {
    const auto assignmentIt = trackingResult.detectionIdentityAssignments.find(detectionIndex);
    if (assignmentIt == trackingResult.detectionIdentityAssignments.end()) {
        return defaultId;
    }
    return assignmentIt->second;
}

const Identity *findConfirmedIdentity(IdentityId identityId, const WriterContext &context) {
    if (identityId == 0) {
        return nullptr;
    }

    const auto identityIt = context.activeTracks.find(identityId);
    if (identityIt == context.activeTracks.end()) {
        return nullptr;
    }

    if (identityIt->second.hitStreak < context.config.minHitsToConfirm) {
        return nullptr;
    }

    return &identityIt->second;
}

void applyAssignedIdentityToDetection(amp::Perception::Rect &rect,
                                      DetectionIndex detectionIndex,
                                      const WriterContext &context,
                                      const TrackingResult &trackingResult) {
    const IdentityId identityId = lookupAssignedIdentityId(detectionIndex, trackingResult);
    const auto *identity = findConfirmedIdentity(identityId, context);
    if (identity == nullptr) {
        return;
    }

    rect.x = identity->lastDetection.x;
    rect.y = identity->lastDetection.y;
    appendIdentityTextIfEnabled(rect, identityId, *identity, context.config);
}

bool shouldEmitTrace(const Identity &identity, const Config &config) {
    return identity.hitStreak >= config.minHitsToConfirm && identity.tracePoints.size() >= 2;
}

void appendTrackTraceDetection(amp::Perception::Layer &traceLayer,
                               IdentityId identityId,
                               const Identity &identity) {
    amp::Perception::TrackTrace trace;
    trace.trackId = identityId;
    trace.parentUuid = identity.lastDetection.parentUuid;
    trace.points.assign(identity.tracePoints.begin(), identity.tracePoints.end());
    traceLayer.detections.push_back(trace);
}

amp::Perception::Layer *ensurePredictionOutputLayer(amp::Perception &perception,
                                                    const Config &config) {
    for (auto &layer : perception.layers) {
        if (isTargetLayer(layer, config)) {
            return &layer;
        }
    }

    amp::Perception::Layer predictedLayer;
    predictedLayer.model = TRACKER_MODEL;
    predictedLayer.engine = TRACKER_ENGINE;
    predictedLayer.tags = PREDICTION_TAG;
    predictedLayer.contentType = config.contentType;
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
            auto *rect = std::get_if<amp::Perception::Rect>(&det);
            if (!rect) {
                continue;
            }

            applyAssignedIdentityToDetection(*rect, detectionIndex, context, trackingResult);

            detectionIndex++;
        }
    }
}

void appendPredictedDetectionsFromTrackingResult(const WriterContext &context,
                                                 const TrackingResult &trackingResult) {
    auto *predictionLayer = ensurePredictionOutputLayer(context.perception, context.config);

    for (const IdentityId identityId : trackingResult.predictedOnlyIdentityIds) {
        const auto *identity = findConfirmedIdentity(identityId, context);
        if (identity == nullptr) {
            continue;
        }

        auto predictedRect = identity->lastDetection;
        appendIdentityTextIfEnabled(predictedRect, identityId, *identity, context.config);

        predictionLayer->detections.push_back(predictedRect);
    }
}

void appendTraceLayerForActiveIdentities(const WriterContext &context) {
    amp::Perception::Layer traceLayer;
    traceLayer.model = TRACKER_MODEL;
    traceLayer.engine = TRACKER_ENGINE;
    traceLayer.tags = TRACE_TAG;
    traceLayer.contentType = "trackTrace";

    for (const auto &[identityId, identity] : context.activeTracks) {
        if (!shouldEmitTrace(identity, context.config)) {
            continue;
        }

        appendTrackTraceDetection(traceLayer, identityId, identity);
    }

    if (!traceLayer.detections.empty()) {
        context.perception.layers.push_back(std::move(traceLayer));
    }
}

} // namespace amp::tracker::trackingoutput
