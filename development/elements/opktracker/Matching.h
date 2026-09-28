/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

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
