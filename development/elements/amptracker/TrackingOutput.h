/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "Tracker.h"

namespace amp::tracker::trackingoutput {

struct WriterContext {
    amp::Perception &perception;
    const ActiveIdentityMap &activeTracks;
    const Config &config;
};

struct TrackingResult {
    const DetectionIdentityAssignments &detectionIdentityAssignments;
    const IdentityIdList &predictedOnlyIdentityIds;
};

/**
 * @brief Updates existing detection rectangles using resolved tracking assignments.
 * @param context Shared output writing context.
 * @param trackingResult Resolved per-frame assignment and prediction outputs.
 */
void updateExistingDetectionsWithTrackingResult(const WriterContext &context,
                                                const TrackingResult &trackingResult);

/**
 * @brief Appends predicted-only detections to the prediction output layer.
 * @param context Shared output writing context.
 * @param trackingResult Resolved per-frame assignment and prediction outputs.
 */
void appendPredictedDetectionsFromTrackingResult(const WriterContext &context,
                                                 const TrackingResult &trackingResult);

/**
 * @brief Appends a trace layer for active tracks.
 * @param context Shared output writing context.
 */
void appendTraceLayerForActiveIdentities(const WriterContext &context);

} // namespace amp::tracker::trackingoutput
