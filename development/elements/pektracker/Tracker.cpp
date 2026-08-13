/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "Tracker.h"

#include "FrameInputs.h"
#include "Matching.h"
#include "TrackLifecycle.h"
#include "TrackingOutput.h"

namespace pek::tracker {

void Tracker::reset() {
    activeTracks.clear();
    inactiveTracks.clear();
    nextTrackId = 1;
    currentFrameIndex = 0;
}

void Tracker::process(perception::FrameResults &frameResults, const Config &config) {
    // Advance the internal frame counter for the current processing step.
    currentFrameIndex++;

    // Gather embedding vectors for this frame.
    const auto embeddings = frameinputs::collectEmbeddings(frameResults, config);
    // Gather trackable detections for this frame in stable processing order.
    auto detections = frameinputs::collectDetections(frameResults, config);

    // Reset per-frame prediction flags before running association.
    matching::clearTrackPredictionFlags(activeTracks);

    // Associate detections with currently active tracks using IoU/ReID cost.
    const auto detectionMatches =
        matching::associateDetectionsToActiveTracks(detections, embeddings, activeTracks, config);

    // Build lifecycle inputs for this frame.
    auto frameTrackingContext = tracklifecycle::FrameTrackingContext{
        detections, embeddings, detectionMatches, currentFrameIndex, config};

    // Build mutable lifecycle state references (active/dormant tracks and next ID).
    auto mutableTrackState =
        tracklifecycle::MutableTrackState{activeTracks, inactiveTracks, nextTrackId};

    // Remove dormant tracks that exceeded the configured retention window.
    tracklifecycle::expireInactiveTracks(frameTrackingContext, mutableTrackState);

    // Update track lifecycle for this frame.
    const auto lifecycleUpdate =
        tracklifecycle::updateTrackLifecycle(frameTrackingContext, mutableTrackState);
    // Bundle lifecycle output needed by output writers.
    const auto resolvedTrackingAssignments = trackingoutput::TrackingResult{
        lifecycleUpdate.assignedTrackByDetection, lifecycleUpdate.predictedOnlyTrackIds};

    // Emit tracker-owned records instead of mutating detector-owned records.
    trackingoutput::appendTrackingPayloads(
        frameResults, detections, activeTracks, config, resolvedTrackingAssignments);
}

} // namespace pek::tracker
