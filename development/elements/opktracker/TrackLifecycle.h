/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

#include "Tracker.h"

namespace opk::tracker::tracklifecycle {

struct UpdateResult {
    DetectionTrackAssignments assignedTrackByDetection;
    TrackIdList predictedOnlyTrackIds;
};

struct FrameTrackingContext {
    const DetectionBatch &detections;
    const EmbeddingBatch &embeddings;
    const AssociationResult &association;
    uint64_t currentFrameIndex;
    double trackerTimeMs;
    float kalmanDt;
    const Config &config;
};

struct MutableTrackState {
    ActiveTrackMap &activeTracks;
    DormantTrackMap &inactiveTracks;
    TrackId &nextTrackId;
};

/**
 * @brief Removes dormant tracks that exceeded the configured history window.
 *
 * @param frameTrackingContext Frame-local immutable lifecycle inputs.
 * @param mutableTrackState Mutable lifecycle state references.
 */
void expireInactiveTracks(const FrameTrackingContext &frameTrackingContext,
                          MutableTrackState &mutableTrackState);

/**
 * @brief Applies matching results and reconciles full track lifecycle for a frame.
 *
 * Updates matched tracks, attempts dormant reassociation, creates new tracks,
 * advances unmatched tracks via prediction, archives expired tracks, and returns
 * assignment/output metadata for downstream writing.
 *
 * @param frameTrackingContext Frame-local immutable lifecycle inputs.
 * @param mutableTrackState Mutable lifecycle state references.
 * @return UpdateResult Detection-to-track assignments and predicted-only tracks.
 */
UpdateResult updateTrackLifecycle(const FrameTrackingContext &frameTrackingContext,
                                  MutableTrackState &mutableTrackState);

} // namespace opk::tracker::tracklifecycle
