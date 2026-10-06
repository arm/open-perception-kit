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

#pragma once

#include "Tracker.h"

namespace opk::tracker::matching {

/**
 * @brief Resets per-frame prediction flags on all active tracks.
 *
 * @param activeTracks Active track state map to update in place.
 */
void clearTrackPredictionFlags(ActiveTrackMap &activeTracks);

/**
 * @brief Associates current detections to active tracks.
 *
 * Computes IoU/embedding-aware costs, solves assignment, and reports matches,
 * unmatched detections, and diagnostics for lifecycle reconciliation.
 *
 * @param detections Ordered detections for the current frame.
 * @param embeddings Embedding lookup keyed by detection ID.
 * @param activeTracks Active track state map.
 * @param config Tracker configuration controlling association behavior.
 * @param kalmanDt Effective Kalman time step for the current frame.
 * @return AssociationResult Matched pairs, diagnostics, and unmatched detections.
 */
AssociationResult associateDetectionsToActiveTracks(const DetectionBatch &detections,
                                                    const EmbeddingBatch &embeddings,
                                                    ActiveTrackMap &activeTracks,
                                                    float kalmanDt,
                                                    const Config &config);

} // namespace opk::tracker::matching
