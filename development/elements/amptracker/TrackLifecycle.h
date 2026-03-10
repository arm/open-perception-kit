/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "Tracker.h"

namespace amp::tracker::tracklifecycle {

struct UpdateResult {
    DetectionIdentityAssignments assignedIdentityByDetection;
    IdentityIdList predictedOnlyIdentityIds;
};

struct FrameTrackingContext {
    const DetectionBatch &detections;
    const EmbeddingBatch &embeddings;
    const AssociationResult &association;
    uint64_t currentFrameIndex;
    const Config &config;
};

struct MutableTrackState {
    ActiveIdentityMap &activeTracks;
    DormantIdentityMap &inactiveTracks;
    IdentityId &nextTrackId;
};

/**
 * @brief Removes dormant identities that exceeded the configured history window.
 *
 * @param frameTrackingContext Frame-local immutable lifecycle inputs.
 * @param mutableTrackState Mutable lifecycle state references.
 */
void expireInactiveTracks(const FrameTrackingContext &frameTrackingContext,
                          MutableTrackState &mutableTrackState);

/**
 * @brief Applies matching results and reconciles full identity lifecycle for a frame.
 *
 * Updates matched identities, attempts dormant reassociation, creates new identities,
 * advances unmatched identities via prediction, archives expired identities, and returns
 * assignment/output metadata for downstream writing.
 *
 * @param frameTrackingContext Frame-local immutable lifecycle inputs.
 * @param mutableTrackState Mutable lifecycle state references.
 * @return UpdateResult Detection-to-identity assignments and predicted-only identities.
 */
UpdateResult updateIdentityLifecycle(const FrameTrackingContext &frameTrackingContext,
                                     MutableTrackState &mutableTrackState);

} // namespace amp::tracker::tracklifecycle
