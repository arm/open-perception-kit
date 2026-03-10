/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "Tracker.h"

namespace amp::tracker::matching {

/**
 * @brief Resets per-frame prediction flags on all active identities.
 *
 * @param activeTracks Active track state map to update in place.
 */
void clearIdentityPredictionFlags(ActiveIdentityMap &activeTracks);

/**
 * @brief Associates current detections to active identities.
 *
 * Computes IoU/embedding-aware costs, solves assignment, and reports matches,
 * unmatched detections, and diagnostics for lifecycle reconciliation.
 *
 * @param detections Ordered detections for the current frame.
 * @param embeddings Embedding lookup keyed by detection UUID.
 * @param activeTracks Active track state map.
 * @param config Tracker configuration controlling association behavior.
 * @return AssociationResult Matched pairs, diagnostics, and unmatched detections.
 */
AssociationResult associateDetectionsToActiveIdentities(const DetectionBatch &detections,
                                                        const EmbeddingBatch &embeddings,
                                                        ActiveIdentityMap &activeTracks,
                                                        const Config &config);

} // namespace amp::tracker::matching
