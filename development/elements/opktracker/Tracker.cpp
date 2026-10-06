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

#include "Tracker.h"

#include "FrameInputs.h"
#include "Matching.h"
#include "TrackLifecycle.h"
#include "TrackingOutput.h"

namespace opk::tracker {

void Tracker::reset() {
    activeTracks.clear();
    inactiveTracks.clear();
    nextTrackId = 1;
    currentFrameIndex = 0;
    kalmanDeltaTime.reset();
}

const KalmanDeltaTimeTracking &Tracker::kalmanDeltaTimeTracking() const {
    return kalmanDeltaTime;
}

void Tracker::process(open_perception_kit::FrameResults &frameResults,
                      const Config &config,
                      std::optional<uint64_t> runningTimeMs) {
    // Advance the internal frame counter for the current processing step.
    currentFrameIndex++;
    kalmanDeltaTime.update(runningTimeMs, config);
    const float kalmanDt = kalmanDeltaTime.effectiveKalmanDt();
    const double trackerTimeMs = kalmanDeltaTime.trackerTimeMs();

    // Gather embedding vectors for this frame.
    const auto embeddings = frameinputs::collectEmbeddings(frameResults, config);
    // Gather trackable detections for this frame in stable processing order.
    auto detections = frameinputs::collectDetections(frameResults, config);

    // Reset per-frame prediction flags before running association.
    matching::clearTrackPredictionFlags(activeTracks);

    // Associate detections with currently active tracks using IoU/ReID cost.
    const auto detectionMatches = matching::associateDetectionsToActiveTracks(
        detections, embeddings, activeTracks, kalmanDt, config);

    // Build lifecycle inputs for this frame.
    auto frameTrackingContext = tracklifecycle::FrameTrackingContext{detections,
                                                                     embeddings,
                                                                     detectionMatches,
                                                                     currentFrameIndex,
                                                                     trackerTimeMs,
                                                                     kalmanDt,
                                                                     config};

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

} // namespace opk::tracker
