/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "Tracker.h"

namespace pek::tracker::matching {

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
 * @return AssociationResult Matched pairs, diagnostics, and unmatched detections.
 */
AssociationResult associateDetectionsToActiveTracks(const DetectionBatch &detections,
                                                    const EmbeddingBatch &embeddings,
                                                    ActiveTrackMap &activeTracks,
                                                    const Config &config);

} // namespace pek::tracker::matching
