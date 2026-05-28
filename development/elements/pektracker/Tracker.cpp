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

void Tracker::process(pek::Perception &perception, const Config &config) {
    // Advance the internal frame counter for the current processing step.
    currentFrameIndex++;

    // Gather embedding vectors (non-owning references) for this frame.
    const auto embeddings = frameinputs::collectEmbeddings(perception, config);
    // Gather trackable detections for this frame in stable processing order.
    auto detections = frameinputs::collectDetections(perception, config);

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
    // Bundle shared output-writing inputs.
    const auto writerContext = trackingoutput::WriterContext{perception, activeTracks, config};
    // Bundle lifecycle output needed by output writers.
    const auto resolvedTrackingAssignments = trackingoutput::TrackingResult{
        lifecycleUpdate.assignedTrackByDetection, lifecycleUpdate.predictedOnlyTrackIds};

    // Write resolved track assignments back onto detection outputs.
    trackingoutput::updateExistingDetectionsWithTrackingResult(writerContext,
                                                               resolvedTrackingAssignments);
    // Add predicted-only tracks into the prediction output layer.
    if (config.emitPredictedDetections) {
        trackingoutput::appendPredictedDetectionsFromTrackingResult(writerContext,
                                                                    resolvedTrackingAssignments);
    }
    // Add track trace for each active track.
    if (config.emitTrace) {
        trackingoutput::appendTraceLayerForActiveTracks(writerContext);
    }
}

<<<<<<< HEAD:development/elements/pektracker/Tracker.cpp
} // namespace pek::tracker
