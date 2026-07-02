/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "TrackLifecycle.h"

#include "Matching.h"
#include "TrackState.h"
#include "Utils.h"

#include <algorithm>
#include <cmath>
#include <fmt/core.h>
#include <optional>

namespace pek::tracker::tracklifecycle {

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
    for (const auto &[trackId, dormant] : mutableTrackState.inactiveTracks) {
        if ((frameTrackingContext.currentFrameIndex - dormant.storedAtFrame) > maxDormantFrames) {
            toErase.push_back(trackId);
        }
    }

    for (const auto trackId : toErase) {
        mutableTrackState.inactiveTracks.erase(trackId);
    }
}

namespace {

struct LifecycleResult {
    DetectionTrackAssignments assignedTrackByDetection;
    TrackIdList predictedOnlyTrackIds;
    TrackIdList tracksToRemove;
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
                           TrackId trackId,
                           const FrameTrackingContext &frameTrackingContext,
                           MutableTrackState &mutableTrackState,
                           LifecycleResult &result) {
    auto &track = mutableTrackState.activeTracks[trackId];
    track.lastDetection = frameTrackingContext.detections[detectionIndex];
    track.missedFrames = 0;
    track.hitStreak++;
    track.lastUpdateFrame = frameTrackingContext.currentFrameIndex;

    pek::Perception::TrackTrace::Point resolvedPoint;
    if (frameTrackingContext.config.useKalman) {
        resolvedPoint = trackstate::correctCenterWithMeasurement(
            track, track.lastDetection, frameTrackingContext.config);
    } else {
        resolvedPoint = {track.lastDetection.x + (track.lastDetection.width * 0.5f),
                         track.lastDetection.y + (track.lastDetection.height * 0.5f)};
        track.predictedThisFrame = false;
    }
    trackstate::appendTracePoint(track, resolvedPoint, frameTrackingContext.config);

    const auto diagnosticIt =
        frameTrackingContext.association.diagnosticsByDetection.find(detectionIndex);
    track.lastMatchDiagnostic =
        (diagnosticIt != frameTrackingContext.association.diagnosticsByDetection.end())
            ? diagnosticIt->second
            : "IOU:N/A";

    const auto embedding = findDetectionEmbedding(detectionIndex, frameTrackingContext);
    if (embedding && isValidEmbedding(embedding->get())) {
        track.lastEmbedding = embedding->get();
        track.hasEmbedding = true;
    }

    track.lastDetection.x = resolvedPoint.x - (track.lastDetection.width * 0.5f);
    track.lastDetection.y = resolvedPoint.y - (track.lastDetection.height * 0.5f);
    result.assignedTrackByDetection[detectionIndex] = trackId;
}

bool tryRestoreDormantTrack(DetectionIndex detectionIndex,
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
    TrackId bestDormantTrackId = 0;
    for (const auto &[dormantTrackId, dormant] : mutableTrackState.inactiveTracks) {
        if (!isValidEmbedding(dormant.lastEmbedding)) {
            continue;
        }

        const float similarity = cosineSimilarity(embedding->get(), dormant.lastEmbedding);
        if (similarity > bestSimilarity) {
            bestSimilarity = similarity;
            bestDormantTrackId = dormantTrackId;
        }
    }

    if (bestDormantTrackId == 0 ||
        bestSimilarity < frameTrackingContext.config.reidReassociateThreshold) {
        return false;
    }

    const auto dormantIt = mutableTrackState.inactiveTracks.find(bestDormantTrackId);
    if (dormantIt == mutableTrackState.inactiveTracks.end()) {
        return false;
    }

    TrackState restoredTrack;
    restoredTrack.trackId = bestDormantTrackId;
    restoredTrack.lastDetection = frameTrackingContext.detections[detectionIndex];
    restoredTrack.lastMatchDiagnostic = fmt::format("REID-R:{:.2f}", bestSimilarity);
    restoredTrack.lastEmbedding = embedding->get();
    restoredTrack.hasEmbedding = true;
    restoredTrack.missedFrames = 0;
    restoredTrack.hitStreak = std::max(1, frameTrackingContext.config.minHitsToConfirm);
    restoredTrack.lastUpdateFrame = frameTrackingContext.currentFrameIndex;

    pek::Perception::TrackTrace::Point initPoint;
    if (frameTrackingContext.config.useKalman) {
        initPoint = trackstate::predictCenter(restoredTrack, frameTrackingContext.config);
        restoredTrack.predictedThisFrame = true;
    } else {
        initPoint = {restoredTrack.lastDetection.x + (restoredTrack.lastDetection.width * 0.5f),
                     restoredTrack.lastDetection.y + (restoredTrack.lastDetection.height * 0.5f)};
        restoredTrack.predictedThisFrame = false;
    }
    trackstate::appendTracePoint(restoredTrack, initPoint, frameTrackingContext.config);
    restoredTrack.lastDetection.x = initPoint.x - (restoredTrack.lastDetection.width * 0.5f);
    restoredTrack.lastDetection.y = initPoint.y - (restoredTrack.lastDetection.height * 0.5f);

    mutableTrackState.activeTracks[bestDormantTrackId] = std::move(restoredTrack);
    result.assignedTrackByDetection[detectionIndex] = bestDormantTrackId;
    mutableTrackState.nextTrackId = std::max(mutableTrackState.nextTrackId, bestDormantTrackId + 1);
    mutableTrackState.inactiveTracks.erase(dormantIt);
    return true;
}

void createTrackFromDetection(DetectionIndex detectionIndex,
                              const FrameTrackingContext &frameTrackingContext,
                              MutableTrackState &mutableTrackState,
                              LifecycleResult &result) {
    TrackState newTrack;
    newTrack.trackId = mutableTrackState.nextTrackId++;
    newTrack.lastDetection = frameTrackingContext.detections[detectionIndex];
    newTrack.lastMatchDiagnostic = "NEW";
    newTrack.missedFrames = 0;
    newTrack.hitStreak = 1;
    newTrack.lastUpdateFrame = frameTrackingContext.currentFrameIndex;

    const auto embedding = findDetectionEmbedding(detectionIndex, frameTrackingContext);
    if (embedding && isValidEmbedding(embedding->get())) {
        newTrack.lastEmbedding = embedding->get();
        newTrack.hasEmbedding = true;
    }

    pek::Perception::TrackTrace::Point initPoint;
    if (frameTrackingContext.config.useKalman) {
        initPoint = trackstate::predictCenter(newTrack, frameTrackingContext.config);
        newTrack.predictedThisFrame = true;
    } else {
        initPoint = {newTrack.lastDetection.x + (newTrack.lastDetection.width * 0.5f),
                     newTrack.lastDetection.y + (newTrack.lastDetection.height * 0.5f)};
        newTrack.predictedThisFrame = false;
    }
    trackstate::appendTracePoint(newTrack, initPoint, frameTrackingContext.config);

    newTrack.lastDetection.x = initPoint.x - (newTrack.lastDetection.width * 0.5f);
    newTrack.lastDetection.y = initPoint.y - (newTrack.lastDetection.height * 0.5f);

    const TrackId newTrackId = newTrack.trackId;
    mutableTrackState.activeTracks.emplace(newTrackId, std::move(newTrack));
    result.assignedTrackByDetection[detectionIndex] = newTrackId;
}

void archiveTrackToDormant(TrackId trackId,
                           const ActiveTrackMap &activeTracks,
                           DormantTrackMap &inactiveTracks,
                           uint64_t currentFrameIndex) {
    const auto trackIt = activeTracks.find(trackId);
    if (trackIt == activeTracks.end()) {
        return;
    }

    if (trackIt->second.hasEmbedding && isValidEmbedding(trackIt->second.lastEmbedding)) {
        DormantTrackState dormant;
        dormant.trackId = trackId;
        dormant.lastDetection = trackIt->second.lastDetection;
        dormant.lastEmbedding = trackIt->second.lastEmbedding;
        dormant.storedAtFrame = currentFrameIndex;
        inactiveTracks[trackId] = std::move(dormant);
    }
}

void updatePredictedOnlyTracks(const FrameTrackingContext &frameTrackingContext,
                               MutableTrackState &mutableTrackState,
                               LifecycleResult &result) {
    for (auto &[trackId, track] : mutableTrackState.activeTracks) {
        if (track.lastUpdateFrame >= frameTrackingContext.currentFrameIndex) {
            continue;
        }

        Perception::TrackTrace::Point predictedPoint{
            track.lastDetection.x + (track.lastDetection.width * 0.5f),
            track.lastDetection.y + (track.lastDetection.height * 0.5f)};
        if (frameTrackingContext.config.useKalman) {
            if (track.predictedThisFrame && track.kalmanInitialized) {
                const auto &state = track.kalman.state();
                predictedPoint = {state[0][0], state[1][0]};
            } else {
                predictedPoint = trackstate::predictCenter(track, frameTrackingContext.config);
            }
        }

        track.lastDetection.x = predictedPoint.x - (track.lastDetection.width * 0.5f);
        track.lastDetection.y = predictedPoint.y - (track.lastDetection.height * 0.5f);
        trackstate::appendTracePoint(track, predictedPoint, frameTrackingContext.config);

        track.missedFrames++;
        if (track.missedFrames <= frameTrackingContext.config.maxMissedFrames &&
            frameTrackingContext.config.emitPredictedDetections) {
            track.lastMatchDiagnostic = "PRED";
            result.predictedOnlyTrackIds.push_back(trackId);
        } else {
            if (track.missedFrames > frameTrackingContext.config.maxMissedFrames) {
                result.tracksToRemove.push_back(trackId);
            }
        }
    }
}

void applyMatchedAssociations(const FrameTrackingContext &frameTrackingContext,
                              MutableTrackState &mutableTrackState,
                              LifecycleResult &result) {
    for (const auto &[detIdx, trackId] : frameTrackingContext.association.matches) {
        applyMatchedDetection(detIdx, trackId, frameTrackingContext, mutableTrackState, result);
    }
}

void resolveUnmatchedDetections(const FrameTrackingContext &frameTrackingContext,
                                MutableTrackState &mutableTrackState,
                                LifecycleResult &result) {
    for (DetectionIndex detIdx : frameTrackingContext.association.unmatchedDetections) {
        if (tryRestoreDormantTrack(detIdx, frameTrackingContext, mutableTrackState, result)) {
            continue;
        }
        createTrackFromDetection(detIdx, frameTrackingContext, mutableTrackState, result);
    }
}

void archiveAndRemoveExpiredTracks(const FrameTrackingContext &frameTrackingContext,
                                   MutableTrackState &mutableTrackState,
                                   const LifecycleResult &result) {
    for (const auto trackId : result.tracksToRemove) {
        archiveTrackToDormant(trackId,
                              mutableTrackState.activeTracks,
                              mutableTrackState.inactiveTracks,
                              frameTrackingContext.currentFrameIndex);
        mutableTrackState.activeTracks.erase(trackId);
    }
}

} // namespace

UpdateResult updateTrackLifecycle(const FrameTrackingContext &frameTrackingContext,
                                  MutableTrackState &mutableTrackState) {
    LifecycleResult lifecycleResult;

    applyMatchedAssociations(frameTrackingContext, mutableTrackState, lifecycleResult);
    resolveUnmatchedDetections(frameTrackingContext, mutableTrackState, lifecycleResult);
    updatePredictedOnlyTracks(frameTrackingContext, mutableTrackState, lifecycleResult);
    archiveAndRemoveExpiredTracks(frameTrackingContext, mutableTrackState, lifecycleResult);

    UpdateResult result;
    result.assignedTrackByDetection = std::move(lifecycleResult.assignedTrackByDetection);
    result.predictedOnlyTrackIds = std::move(lifecycleResult.predictedOnlyTrackIds);
    return result;
}

} // namespace pek::tracker::tracklifecycle
