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

#include "TrackLifecycle.h"

#include "Matching.h"
#include "TrackState.h"
#include "Utils.h"

#include <algorithm>
#include <cassert>
#include <fmt/core.h>
#include <memory>

namespace opk::tracker::tracklifecycle {

void expireInactiveTracks(const FrameTrackingContext &frameTrackingContext,
                          MutableTrackState &mutableTrackState) {
    if (mutableTrackState.inactiveTracks.empty()) {
        return;
    }

    const double maxDormantTimeMs =
        static_cast<double>(
            std::max(frameTrackingContext.config.dormantTrackHistorySeconds, 0.0f)) *
        1'000.0;

    std::vector<uint64_t> toErase;
    toErase.reserve(mutableTrackState.inactiveTracks.size());
    for (const auto &[trackId, dormant] : mutableTrackState.inactiveTracks) {
        const double dormantTimeMs =
            frameTrackingContext.trackerTimeMs - dormant.storedAtTrackerTimeMs;
        if (dormantTimeMs > maxDormantTimeMs) {
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

open_perception_kit::metadata::BoundingBoxT &
ensureBox(open_perception_kit::metadata::BoxDetectionT &detection) {
    if (!detection.box) {
        detection.box = std::make_unique<open_perception_kit::metadata::BoundingBoxT>();
    }
    return *detection.box;
}

const open_perception_kit::metadata::BoxDetectionT &detectionAt(const DetectionBatch &detections,
                                                                DetectionIndex detectionIndex) {
    assert(detectionIndex < detections.size());
    assert(detections[detectionIndex] != nullptr);
    return *detections[detectionIndex];
}

Point2f makePoint(float x, float y) {
    return Point2f{x, y};
}

const std::vector<float> *findDetectionEmbedding(DetectionIndex detectionIndex,
                                                 const FrameTrackingContext &frameTrackingContext) {
    const auto &detection = detectionAt(frameTrackingContext.detections, detectionIndex);
    if (!detection.object) {
        return nullptr;
    }

    const auto embeddingIt = frameTrackingContext.embeddings.find(detection.object->id);
    if (embeddingIt == frameTrackingContext.embeddings.end() || embeddingIt->second == nullptr) {
        return nullptr;
    }
    return embeddingIt->second;
}

void applyMatchedDetection(DetectionIndex detectionIndex,
                           TrackId trackId,
                           const FrameTrackingContext &frameTrackingContext,
                           MutableTrackState &mutableTrackState,
                           LifecycleResult &result) {
    auto &track = mutableTrackState.activeTracks[trackId];
    track.lastDetection = detectionAt(frameTrackingContext.detections, detectionIndex);
    track.missedFrames = 0;
    track.hitStreak++;
    track.lastUpdateFrame = frameTrackingContext.currentFrameIndex;

    Point2f resolvedPoint;
    if (frameTrackingContext.config.useKalman) {
        resolvedPoint = trackstate::correctCenterWithMeasurement(
            track, track.lastDetection, frameTrackingContext.kalmanDt, frameTrackingContext.config);
    } else {
        const auto &lastBox = ensureBox(track.lastDetection);
        resolvedPoint =
            makePoint(lastBox.x + (lastBox.width * 0.5f), lastBox.y + (lastBox.height * 0.5f));
        track.predictedThisFrame = false;
    }
    trackstate::appendTracePoint(
        track, resolvedPoint, frameTrackingContext.kalmanDt, frameTrackingContext.config);

    const auto diagnosticIt =
        frameTrackingContext.association.diagnosticsByDetection.find(detectionIndex);
    track.lastMatchDiagnostic =
        (diagnosticIt != frameTrackingContext.association.diagnosticsByDetection.end())
            ? diagnosticIt->second
            : "IOU:N/A";

    if (const auto embedding = findDetectionEmbedding(detectionIndex, frameTrackingContext);
        embedding != nullptr && isValidEmbedding(*embedding)) {
        track.lastEmbedding = *embedding;
        track.hasEmbedding = true;
    }

    auto &lastBox = ensureBox(track.lastDetection);
    lastBox.x = resolvedPoint.x - (lastBox.width * 0.5f);
    lastBox.y = resolvedPoint.y - (lastBox.height * 0.5f);
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
    if (embedding == nullptr || !isValidEmbedding(*embedding)) {
        return false;
    }

    float bestSimilarity = -1.0f;
    TrackId bestDormantTrackId = 0;
    for (const auto &[dormantTrackId, dormant] : mutableTrackState.inactiveTracks) {
        if (!isValidEmbedding(dormant.lastEmbedding)) {
            continue;
        }

        const float similarity = cosineSimilarity(*embedding, dormant.lastEmbedding);
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
    restoredTrack.lastDetection = detectionAt(frameTrackingContext.detections, detectionIndex);
    restoredTrack.lastMatchDiagnostic = fmt::format("REID-R:{:.2f}", bestSimilarity);
    restoredTrack.lastEmbedding = *embedding;
    restoredTrack.hasEmbedding = true;
    restoredTrack.missedFrames = 0;
    restoredTrack.hitStreak = std::max(1, frameTrackingContext.config.minHitsToConfirm);
    restoredTrack.lastUpdateFrame = frameTrackingContext.currentFrameIndex;

    Point2f initPoint;
    if (frameTrackingContext.config.useKalman) {
        initPoint = trackstate::predictCenter(
            restoredTrack, frameTrackingContext.kalmanDt, frameTrackingContext.config);
        restoredTrack.predictedThisFrame = true;
    } else {
        const auto &restoredBox = ensureBox(restoredTrack.lastDetection);
        initPoint = makePoint(restoredBox.x + (restoredBox.width * 0.5f),
                              restoredBox.y + (restoredBox.height * 0.5f));
        restoredTrack.predictedThisFrame = false;
    }
    trackstate::appendTracePoint(
        restoredTrack, initPoint, frameTrackingContext.kalmanDt, frameTrackingContext.config);
    auto &restoredBox = ensureBox(restoredTrack.lastDetection);
    restoredBox.x = initPoint.x - (restoredBox.width * 0.5f);
    restoredBox.y = initPoint.y - (restoredBox.height * 0.5f);

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
    newTrack.lastDetection = detectionAt(frameTrackingContext.detections, detectionIndex);
    newTrack.lastMatchDiagnostic = "NEW";
    newTrack.missedFrames = 0;
    newTrack.hitStreak = 1;
    newTrack.lastUpdateFrame = frameTrackingContext.currentFrameIndex;

    if (const auto embedding = findDetectionEmbedding(detectionIndex, frameTrackingContext);
        embedding != nullptr && isValidEmbedding(*embedding)) {
        newTrack.lastEmbedding = *embedding;
        newTrack.hasEmbedding = true;
    }

    Point2f initPoint;
    if (frameTrackingContext.config.useKalman) {
        initPoint = trackstate::predictCenter(
            newTrack, frameTrackingContext.kalmanDt, frameTrackingContext.config);
        newTrack.predictedThisFrame = true;
    } else {
        const auto &newBox = ensureBox(newTrack.lastDetection);
        initPoint = makePoint(newBox.x + (newBox.width * 0.5f), newBox.y + (newBox.height * 0.5f));
        newTrack.predictedThisFrame = false;
    }
    trackstate::appendTracePoint(
        newTrack, initPoint, frameTrackingContext.kalmanDt, frameTrackingContext.config);

    auto &newBox = ensureBox(newTrack.lastDetection);
    newBox.x = initPoint.x - (newBox.width * 0.5f);
    newBox.y = initPoint.y - (newBox.height * 0.5f);

    const TrackId newTrackId = newTrack.trackId;
    mutableTrackState.activeTracks.emplace(newTrackId, std::move(newTrack));
    result.assignedTrackByDetection[detectionIndex] = newTrackId;
}

void archiveTrackToDormant(TrackId trackId,
                           const ActiveTrackMap &activeTracks,
                           DormantTrackMap &inactiveTracks,
                           double trackerTimeMs) {
    const auto trackIt = activeTracks.find(trackId);
    if (trackIt == activeTracks.end()) {
        return;
    }

    if (trackIt->second.hasEmbedding && isValidEmbedding(trackIt->second.lastEmbedding)) {
        DormantTrackState dormant;
        dormant.trackId = trackId;
        dormant.lastDetection = trackIt->second.lastDetection;
        dormant.lastEmbedding = trackIt->second.lastEmbedding;
        dormant.storedAtTrackerTimeMs = trackerTimeMs;
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

        auto &lastBox = ensureBox(track.lastDetection);
        Point2f predictedPoint =
            makePoint(lastBox.x + (lastBox.width * 0.5f), lastBox.y + (lastBox.height * 0.5f));
        if (frameTrackingContext.config.useKalman) {
            if (track.predictedThisFrame && track.kalmanInitialized) {
                const auto &state = track.kalman.state();
                predictedPoint = makePoint(state[0][0], state[1][0]);
            } else {
                predictedPoint = trackstate::predictCenter(
                    track, frameTrackingContext.kalmanDt, frameTrackingContext.config);
            }
        }

        lastBox.x = predictedPoint.x - (lastBox.width * 0.5f);
        lastBox.y = predictedPoint.y - (lastBox.height * 0.5f);
        trackstate::appendTracePoint(
            track, predictedPoint, frameTrackingContext.kalmanDt, frameTrackingContext.config);

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
                              frameTrackingContext.trackerTimeMs);
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

} // namespace opk::tracker::tracklifecycle
