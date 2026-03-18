/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "Matching.h"

#include "TrackState.h"
#include "Utils.h"

#include "algo/Hungarian.h"
#include "algo/IoU.h"

#include <algorithm>
#include <cmath>
#include <fmt/core.h>
#include <optional>

namespace amp::tracker::matching {

namespace {

using CostMatrix = std::vector<std::vector<float>>;
using SimilarityMatrix = std::vector<std::vector<std::optional<float>>>;

void markAllDetectionsUnmatched(DetectionIndex detectionCount, AssociationResult &result) {
    result.unmatchedDetections.resize(detectionCount);
    for (size_t detIdx = 0; detIdx < detectionCount; ++detIdx) {
        result.unmatchedDetections[detIdx] = detIdx;
    }
}

TrackIdList collectActiveTrackIds(const ActiveTrackMap &activeTracks) {
    TrackIdList trackIds;
    trackIds.reserve(activeTracks.size());
    for (const auto &[trackId, _] : activeTracks) {
        trackIds.push_back(trackId);
    }
    return trackIds;
}

std::vector<amp::Perception::Rect> predictTrackBoxes(ActiveTrackMap &activeTracks,
                                                     const Config &config) {
    std::vector<amp::Perception::Rect> predictedTrackBoxes;
    predictedTrackBoxes.reserve(activeTracks.size());

    for (auto &[trackId, track] : activeTracks) {
        (void)trackId;
        const auto predictedPoint = trackstate::predictCenter(track, config);
        track.predictedThisFrame = true;

        auto predictedRect = track.lastDetection;
        predictedRect.x = predictedPoint.x - (predictedRect.width * 0.5f);
        predictedRect.y = predictedPoint.y - (predictedRect.height * 0.5f);
        predictedTrackBoxes.push_back(predictedRect);
    }

    return predictedTrackBoxes;
}

const std::vector<float> *findDetectionEmbedding(size_t detectionIndex,
                                                 const DetectionBatch &detections,
                                                 const EmbeddingBatch &embeddings) {
    const auto embeddingIt = embeddings.find(detections[detectionIndex].uuid);
    if (embeddingIt == embeddings.end()) {
        return nullptr;
    }
    return &embeddingIt->second.get();
}

std::optional<float> computeValidSimilarity(size_t detectionIndex,
                                            const DetectionBatch &detections,
                                            TrackId trackId,
                                            const EmbeddingBatch &embeddings,
                                            const ActiveTrackMap &activeTracks,
                                            const Config &config) {
    const auto *embedding = findDetectionEmbedding(detectionIndex, detections, embeddings);
    const auto trackIt = activeTracks.find(trackId);
    if (embedding == nullptr || trackIt == activeTracks.end() || !trackIt->second.hasEmbedding) {
        return std::nullopt;
    }

    const float similarity = cosineSimilarity(*embedding, trackIt->second.lastEmbedding);
    if (similarity < config.minCosineSimilarity) {
        return std::nullopt;
    }

    return similarity;
}

float computeAssociationCost(float iou,
                             const std::optional<float> &similarity,
                             const Config &config) {
    const float iouCost = 1.0f - iou;

    if (!config.useEmbeddings || config.embeddingWeight <= 0.0f || !similarity.has_value()) {
        return iouCost;
    }

    //
    // Convert cosine similarity from [-1, 1] into [0, 1], then flip it into a
    // cost where lower is better:
    //   normalizedSimilarity = (similarity + 1) / 2
    //   embeddingCost = 1 - normalizedSimilarity
    //
    // Blend IoU cost and embedding cost using embeddingWeight:
    //   finalCost = (1 - w) * iouCost + w * embeddingCost
    // where w is clamped to [0, 1].
    //
    const float embeddingCost = 1.0f - (((*similarity) + 1.0f) * 0.5f);
    const float weight = std::clamp(config.embeddingWeight, 0.0f, 1.0f);
    return ((1.0f - weight) * iouCost) + (weight * embeddingCost);
}

std::string buildMatchDiagnostic(float iou, const std::optional<float> &similarity) {
    if (similarity.has_value()) {
        return fmt::format("REID:{:.2f}", *similarity);
    }
    return fmt::format("IOU:{:.2f}", iou);
}

void buildAssociationMatrices(const DetectionBatch &detections,
                              const TrackIdList &trackIds,
                              const std::vector<amp::Perception::Rect> &predictedTrackBoxes,
                              const EmbeddingBatch &embeddings,
                              const ActiveTrackMap &activeTracks,
                              const Config &config,
                              CostMatrix &iouMatrix,
                              CostMatrix &costMatrix,
                              SimilarityMatrix &similarityMatrix) {
    iouMatrix.assign(detections.size(), std::vector<float>(trackIds.size(), 0.0f));
    costMatrix.assign(detections.size(), std::vector<float>(trackIds.size(), 1.0f));
    similarityMatrix.assign(detections.size(),
                            std::vector<std::optional<float>>(trackIds.size(), std::nullopt));

    for (size_t detIdx = 0; detIdx < detections.size(); ++detIdx) {
        const auto &det = detections[detIdx];
        for (size_t trackIdx = 0; trackIdx < trackIds.size(); ++trackIdx) {
            const float iou = amp::algo::computeIoU(det, predictedTrackBoxes[trackIdx]);
            iouMatrix[detIdx][trackIdx] = iou;
            const auto similarity = computeValidSimilarity(
                detIdx, detections, trackIds[trackIdx], embeddings, activeTracks, config);
            similarityMatrix[detIdx][trackIdx] = similarity;
            costMatrix[detIdx][trackIdx] = computeAssociationCost(iou, similarity, config);
        }
    }
}

void collectMatchesFromAssignment(const DetectionBatch &detections,
                                  const std::vector<int> &assignment,
                                  const TrackIdList &trackIds,
                                  const CostMatrix &iouMatrix,
                                  const SimilarityMatrix &similarityMatrix,
                                  const Config &config,
                                  AssociationResult &result) {
    std::vector<bool> matchedDetection(detections.size(), false);

    for (size_t detIdx = 0; detIdx < assignment.size() && detIdx < detections.size(); ++detIdx) {
        const int trackIdx = assignment[detIdx];
        if (trackIdx < 0 || static_cast<size_t>(trackIdx) >= trackIds.size()) {
            continue;
        }

        const float iou = iouMatrix[detIdx][static_cast<size_t>(trackIdx)];
        if (iou < config.iouThreshold) {
            continue;
        }

        const TrackId matchedTrackId = trackIds[static_cast<size_t>(trackIdx)];
        result.matches.push_back({detIdx, matchedTrackId});
        result.diagnosticsByDetection[detIdx] =
            buildMatchDiagnostic(iou, similarityMatrix[detIdx][static_cast<size_t>(trackIdx)]);
        matchedDetection[detIdx] = true;
    }

    for (size_t detIdx = 0; detIdx < detections.size(); ++detIdx) {
        if (!matchedDetection[detIdx]) {
            result.unmatchedDetections.push_back(detIdx);
        }
    }
}

} // namespace

void clearTrackPredictionFlags(ActiveTrackMap &activeTracks) {
    for (auto &[trackId, track] : activeTracks) {
        (void)trackId;
        trackstate::clearPredictionFlag(track);
    }
}

AssociationResult associateDetectionsToActiveTracks(const DetectionBatch &detections,
                                                    const EmbeddingBatch &embeddings,
                                                    ActiveTrackMap &activeTracks,
                                                    const Config &config) {
    AssociationResult result;

    if (detections.empty()) {
        return result;
    }

    if (activeTracks.empty()) {
        markAllDetectionsUnmatched(detections.size(), result);
        return result;
    }

    const auto trackIds = collectActiveTrackIds(activeTracks);
    const auto predictedTrackBoxes = predictTrackBoxes(activeTracks, config);
    CostMatrix iouMatrix;
    CostMatrix costMatrix;
    SimilarityMatrix similarityMatrix;
    buildAssociationMatrices(detections,
                             trackIds,
                             predictedTrackBoxes,
                             embeddings,
                             activeTracks,
                             config,
                             iouMatrix,
                             costMatrix,
                             similarityMatrix);

    const auto assignment = amp::algo::solveHungarian(costMatrix);

    collectMatchesFromAssignment(
        detections, assignment, trackIds, iouMatrix, similarityMatrix, config, result);

    return result;
}

} // namespace amp::tracker::matching
