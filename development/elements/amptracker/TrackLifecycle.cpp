/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "TrackLifecycle.h"

#include "Identity.h"
#include "Matching.h"
#include "Utils.h"

#include <algorithm>
#include <cmath>
#include <fmt/core.h>
#include <optional>

namespace amp::tracker::tracklifecycle {

void expireInactiveTracks(const FrameTrackingContext &frameTrackingContext,
                          MutableTrackState &mutableTrackState) {
    if (mutableTrackState.inactiveTracks.empty()) {
        return;
    }

    const float dt = std::max(frameTrackingContext.config.kalmanDt, 1e-4f);
    const uint64_t maxDormantFrames = static_cast<uint64_t>(std::max(
        1.0f,
        std::ceil(std::max(frameTrackingContext.config.dormantTrackHistorySeconds, 0.0f) / dt)));

    std::vector<uint64_t> toErase;
    toErase.reserve(mutableTrackState.inactiveTracks.size());
    for (const auto &[identityId, dormant] : mutableTrackState.inactiveTracks) {
        if ((frameTrackingContext.currentFrameIndex - dormant.storedAtFrame) > maxDormantFrames) {
            toErase.push_back(identityId);
        }
    }

    for (const auto identityId : toErase) {
        mutableTrackState.inactiveTracks.erase(identityId);
    }
}

namespace {

struct LifecycleResult {
    DetectionIdentityAssignments assignedIdentityByDetection;
    IdentityIdList predictedOnlyIdentityIds;
    IdentityIdList identitiesToRemove;
};

std::optional<std::reference_wrapper<const std::vector<float>>>
findDetectionEmbedding(DetectionIndex detectionIndex,
                       const FrameTrackingContext &frameTrackingContext) {
    const auto embeddingIt =
        frameTrackingContext.embeddings.find(frameTrackingContext.detections[detectionIndex].uuid);
    if (embeddingIt == frameTrackingContext.embeddings.end()) {
        return std::nullopt;
    }
    return embeddingIt->second;
}

void applyMatchedDetection(DetectionIndex detectionIndex,
                           IdentityId identityId,
                           const FrameTrackingContext &frameTrackingContext,
                           MutableTrackState &mutableTrackState,
                           LifecycleResult &result) {
    auto &identity = mutableTrackState.activeTracks[identityId];
    identity.lastDetection = frameTrackingContext.detections[detectionIndex];
    identity.missedFrames = 0;
    identity.hitStreak++;
    identity.lastUpdateFrame = frameTrackingContext.currentFrameIndex;

    const auto smoothedPoint = identity::correctCenterWithMeasurement(
        identity, identity.lastDetection, frameTrackingContext.config);
    identity::appendTraceSample(identity, smoothedPoint, frameTrackingContext.config);

    const auto diagnosticIt =
        frameTrackingContext.association.diagnosticsByDetection.find(detectionIndex);
    identity.lastMatchDiagnostic =
        (diagnosticIt != frameTrackingContext.association.diagnosticsByDetection.end())
            ? diagnosticIt->second
            : "IOU:N/A";

    const auto embedding = findDetectionEmbedding(detectionIndex, frameTrackingContext);
    if (embedding && isValidEmbedding(embedding->get())) {
        identity.lastEmbedding = embedding->get();
        identity.hasEmbedding = true;
    }

    identity.lastDetection.x = smoothedPoint.x - (identity.lastDetection.width * 0.5f);
    identity.lastDetection.y = smoothedPoint.y - (identity.lastDetection.height * 0.5f);
    result.assignedIdentityByDetection[detectionIndex] = identityId;
}

bool tryRestoreDormantIdentity(DetectionIndex detectionIndex,
                               const FrameTrackingContext &frameTrackingContext,
                               MutableTrackState &mutableTrackState,
                               LifecycleResult &result) {
    if (!frameTrackingContext.config.useEmbeddings ||
        frameTrackingContext.config.reidReassociateThreshold <= 0.0f ||
        mutableTrackState.inactiveTracks.empty()) {
        return false;
    }

    const auto embedding = findDetectionEmbedding(detectionIndex, frameTrackingContext);
    if (!embedding || !isValidEmbedding(embedding->get())) {
        return false;
    }

    float bestSimilarity = -1.0f;
    IdentityId bestDormantIdentityId = 0;
    for (const auto &[dormantIdentityId, dormant] : mutableTrackState.inactiveTracks) {
        if (!isValidEmbedding(dormant.lastEmbedding)) {
            continue;
        }

        const float similarity = cosineSimilarity(embedding->get(), dormant.lastEmbedding);
        if (similarity > bestSimilarity) {
            bestSimilarity = similarity;
            bestDormantIdentityId = dormantIdentityId;
        }
    }

    if (bestDormantIdentityId == 0 ||
        bestSimilarity < frameTrackingContext.config.reidReassociateThreshold) {
        return false;
    }

    const auto dormantIt = mutableTrackState.inactiveTracks.find(bestDormantIdentityId);
    if (dormantIt == mutableTrackState.inactiveTracks.end()) {
        return false;
    }

    Identity restoredIdentity;
    restoredIdentity.identityId = bestDormantIdentityId;
    restoredIdentity.lastDetection = frameTrackingContext.detections[detectionIndex];
    restoredIdentity.lastMatchDiagnostic = fmt::format("REID-R:{:.2f}", bestSimilarity);
    restoredIdentity.lastEmbedding = embedding->get();
    restoredIdentity.hasEmbedding = true;
    restoredIdentity.missedFrames = 0;
    restoredIdentity.hitStreak = std::max(1, frameTrackingContext.config.minHitsToConfirm);
    restoredIdentity.lastUpdateFrame = frameTrackingContext.currentFrameIndex;

    const auto initPoint = identity::predictCenter(restoredIdentity, frameTrackingContext.config);
    restoredIdentity.predictedThisFrame = true;
    identity::appendTraceSample(restoredIdentity, initPoint, frameTrackingContext.config);
    restoredIdentity.lastDetection.x = initPoint.x - (restoredIdentity.lastDetection.width * 0.5f);
    restoredIdentity.lastDetection.y = initPoint.y - (restoredIdentity.lastDetection.height * 0.5f);

    mutableTrackState.activeTracks[bestDormantIdentityId] = std::move(restoredIdentity);
    result.assignedIdentityByDetection[detectionIndex] = bestDormantIdentityId;
    mutableTrackState.nextTrackId =
        std::max(mutableTrackState.nextTrackId, bestDormantIdentityId + 1);
    mutableTrackState.inactiveTracks.erase(dormantIt);
    return true;
}

void createIdentityFromDetection(DetectionIndex detectionIndex,
                                 const FrameTrackingContext &frameTrackingContext,
                                 MutableTrackState &mutableTrackState,
                                 LifecycleResult &result) {
    Identity newIdentity;
    newIdentity.identityId = mutableTrackState.nextTrackId++;
    newIdentity.lastDetection = frameTrackingContext.detections[detectionIndex];
    newIdentity.lastMatchDiagnostic = "NEW";
    newIdentity.missedFrames = 0;
    newIdentity.hitStreak = 1;
    newIdentity.lastUpdateFrame = frameTrackingContext.currentFrameIndex;

    const auto embedding = findDetectionEmbedding(detectionIndex, frameTrackingContext);
    if (embedding && isValidEmbedding(embedding->get())) {
        newIdentity.lastEmbedding = embedding->get();
        newIdentity.hasEmbedding = true;
    }

    const auto initPoint = identity::predictCenter(newIdentity, frameTrackingContext.config);
    newIdentity.predictedThisFrame = true;
    identity::appendTraceSample(newIdentity, initPoint, frameTrackingContext.config);

    newIdentity.lastDetection.x = initPoint.x - (newIdentity.lastDetection.width * 0.5f);
    newIdentity.lastDetection.y = initPoint.y - (newIdentity.lastDetection.height * 0.5f);

    const IdentityId newIdentityId = newIdentity.identityId;
    mutableTrackState.activeTracks.emplace(newIdentityId, std::move(newIdentity));
    result.assignedIdentityByDetection[detectionIndex] = newIdentityId;
}

void archiveIdentityToDormant(IdentityId identityId,
                              const ActiveIdentityMap &activeTracks,
                              DormantIdentityMap &inactiveTracks,
                              uint64_t currentFrameIndex) {
    const auto identityIt = activeTracks.find(identityId);
    if (identityIt == activeTracks.end()) {
        return;
    }

    if (identityIt->second.hasEmbedding && isValidEmbedding(identityIt->second.lastEmbedding)) {
        DormantIdentity dormant;
        dormant.identityId = identityId;
        dormant.lastDetection = identityIt->second.lastDetection;
        dormant.lastEmbedding = identityIt->second.lastEmbedding;
        dormant.storedAtFrame = currentFrameIndex;
        inactiveTracks[identityId] = std::move(dormant);
    }
}

void updatePredictedOnlyIdentities(const FrameTrackingContext &frameTrackingContext,
                                   MutableTrackState &mutableTrackState,
                                   LifecycleResult &result) {
    for (auto &[identityId, identity] : mutableTrackState.activeTracks) {
        if (identity.lastUpdateFrame >= frameTrackingContext.currentFrameIndex) {
            continue;
        }

        Perception::TrackTrace::Point predictedPoint;
        if (identity.predictedThisFrame && identity.kalmanInitialized) {
            const auto &state = identity.kalman.state();
            predictedPoint = {state[0][0], state[1][0]};
        } else {
            predictedPoint = identity::predictCenter(identity, frameTrackingContext.config);
        }

        identity.lastDetection.x = predictedPoint.x - (identity.lastDetection.width * 0.5f);
        identity.lastDetection.y = predictedPoint.y - (identity.lastDetection.height * 0.5f);
        identity::appendTraceSample(identity, predictedPoint, frameTrackingContext.config);

        identity.missedFrames++;
        if (identity.missedFrames <= frameTrackingContext.config.maxMissedFrames) {
            identity.lastMatchDiagnostic = "PRED";
            result.predictedOnlyIdentityIds.push_back(identityId);
        } else {
            result.identitiesToRemove.push_back(identityId);
        }
    }
}

void applyMatchedAssociations(const FrameTrackingContext &frameTrackingContext,
                              MutableTrackState &mutableTrackState,
                              LifecycleResult &result) {
    for (const auto &[detIdx, identityId] : frameTrackingContext.association.matches) {
        applyMatchedDetection(detIdx, identityId, frameTrackingContext, mutableTrackState, result);
    }
}

void resolveUnmatchedDetections(const FrameTrackingContext &frameTrackingContext,
                                MutableTrackState &mutableTrackState,
                                LifecycleResult &result) {
    for (DetectionIndex detIdx : frameTrackingContext.association.unmatchedDetections) {
        if (tryRestoreDormantIdentity(detIdx, frameTrackingContext, mutableTrackState, result)) {
            continue;
        }
        createIdentityFromDetection(detIdx, frameTrackingContext, mutableTrackState, result);
    }
}

void archiveAndRemoveExpiredIdentities(const FrameTrackingContext &frameTrackingContext,
                                       MutableTrackState &mutableTrackState,
                                       const LifecycleResult &result) {
    for (const auto identityId : result.identitiesToRemove) {
        archiveIdentityToDormant(identityId,
                                 mutableTrackState.activeTracks,
                                 mutableTrackState.inactiveTracks,
                                 frameTrackingContext.currentFrameIndex);
        mutableTrackState.activeTracks.erase(identityId);
    }
}

} // namespace

UpdateResult updateIdentityLifecycle(const FrameTrackingContext &frameTrackingContext,
                                     MutableTrackState &mutableTrackState) {
    LifecycleResult lifecycleResult;

    applyMatchedAssociations(frameTrackingContext, mutableTrackState, lifecycleResult);
    resolveUnmatchedDetections(frameTrackingContext, mutableTrackState, lifecycleResult);
    updatePredictedOnlyIdentities(frameTrackingContext, mutableTrackState, lifecycleResult);
    archiveAndRemoveExpiredIdentities(frameTrackingContext, mutableTrackState, lifecycleResult);

    UpdateResult result;
    result.assignedIdentityByDetection = std::move(lifecycleResult.assignedIdentityByDetection);
    result.predictedOnlyIdentityIds = std::move(lifecycleResult.predictedOnlyIdentityIds);
    return result;
}

} // namespace amp::tracker::tracklifecycle
