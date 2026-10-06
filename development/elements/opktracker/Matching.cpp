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

#include "Matching.h"

#include "TrackState.h"
#include "Utils.h"

#include "algo/Hungarian.h"
#include "algo/IoU.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <fmt/core.h>
#include <optional>

namespace opk::tracker::matching {

namespace {

using CostMatrix = std::vector<std::vector<float>>;
using SimilarityMatrix = std::vector<std::vector<std::optional<float>>>;

struct AssociationMatrices {
    CostMatrix iou;
    CostMatrix cost;
    SimilarityMatrix similarity;
};

const open_perception_kit::metadata::BoxDetectionT &detectionAt(const DetectionBatch &detections,
                                                                size_t index) {
    assert(index < detections.size());
    assert(detections[index] != nullptr);
    return *detections[index];
}

uint64_t idOf(const open_perception_kit::metadata::BoxDetectionT &detection) {
    return detection.object ? detection.object->id : 0U;
}

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

std::vector<open_perception_kit::metadata::BoundingBoxT>
predictTrackBoxes(ActiveTrackMap &activeTracks, float kalmanDt, const Config &config) {
    std::vector<open_perception_kit::metadata::BoundingBoxT> predictedTrackBoxes;
    predictedTrackBoxes.reserve(activeTracks.size());

    for (auto &[trackId, track] : activeTracks) {
        (void)trackId;
        assert(track.lastDetection.box);
        auto predictedBox = *track.lastDetection.box;
        if (config.useKalman) {
            const auto predictedPoint = trackstate::predictCenter(track, kalmanDt, config);
            track.predictedThisFrame = true;
            predictedBox.x = predictedPoint.x - (predictedBox.width * 0.5f);
            predictedBox.y = predictedPoint.y - (predictedBox.height * 0.5f);
        }
        predictedTrackBoxes.push_back(predictedBox);
    }

    return predictedTrackBoxes;
}

const std::vector<float> *findDetectionEmbedding(size_t detectionIndex,
                                                 const DetectionBatch &detections,
                                                 const EmbeddingBatch &embeddings) {
    const auto embeddingIt = embeddings.find(idOf(detectionAt(detections, detectionIndex)));
    if (embeddingIt == embeddings.end() || embeddingIt->second == nullptr) {
        return nullptr;
    }
    return embeddingIt->second;
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

    if (config.associationMode == AssociationMode::Iou) {
        return iouCost;
    }

    if (config.associationMode == AssociationMode::Embedding) {
        if (!similarity.has_value()) {
            return 1.0f;
        }
        return 1.0f - (((*similarity) + 1.0f) * 0.5f);
    }

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

bool isAssignmentAccepted(float iou, const std::optional<float> &similarity, const Config &config) {
    switch (config.associationMode) {
    case AssociationMode::Iou:
        return iou >= config.iouThreshold;
    case AssociationMode::Embedding:
        return similarity.has_value();
    case AssociationMode::Hybrid:
        return iou >= config.iouThreshold;
    }

    return false;
}

AssociationMatrices buildAssociationMatrices(
    const DetectionBatch &detections,
    const TrackIdList &trackIds,
    const std::vector<open_perception_kit::metadata::BoundingBoxT> &predictedTrackBoxes,
    const EmbeddingBatch &embeddings,
    const ActiveTrackMap &activeTracks,
    const Config &config) {
    AssociationMatrices matrices{
        .iou = CostMatrix(detections.size(), std::vector<float>(trackIds.size(), 0.0f)),
        .cost = CostMatrix(detections.size(), std::vector<float>(trackIds.size(), 1.0f)),
        .similarity = SimilarityMatrix(
            detections.size(), std::vector<std::optional<float>>(trackIds.size(), std::nullopt)),
    };

    for (size_t detIdx = 0; detIdx < detections.size(); ++detIdx) {
        const auto &det = detectionAt(detections, detIdx);
        assert(det.box);
        for (size_t trackIdx = 0; trackIdx < trackIds.size(); ++trackIdx) {
            const float iou = opk::algo::computeIoU(*det.box, predictedTrackBoxes[trackIdx]);
            matrices.iou[detIdx][trackIdx] = iou;
            const auto similarity = computeValidSimilarity(
                detIdx, detections, trackIds[trackIdx], embeddings, activeTracks, config);
            matrices.similarity[detIdx][trackIdx] = similarity;
            matrices.cost[detIdx][trackIdx] = computeAssociationCost(iou, similarity, config);
        }
    }

    return matrices;
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
        const auto &similarity = similarityMatrix[detIdx][static_cast<size_t>(trackIdx)];
        if (!isAssignmentAccepted(iou, similarity, config)) {
            continue;
        }

        const TrackId matchedTrackId = trackIds[static_cast<size_t>(trackIdx)];
        result.matches.emplace_back(detIdx, matchedTrackId);
        result.diagnosticsByDetection[detIdx] = buildMatchDiagnostic(iou, similarity);
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
                                                    float kalmanDt,
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
    const auto predictedTrackBoxes = predictTrackBoxes(activeTracks, kalmanDt, config);
    const auto matrices = buildAssociationMatrices(
        detections, trackIds, predictedTrackBoxes, embeddings, activeTracks, config);

    const auto assignment = opk::algo::solveHungarian(matrices.cost);

    collectMatchesFromAssignment(
        detections, assignment, trackIds, matrices.iou, matrices.similarity, config, result);

    return result;
}

} // namespace opk::tracker::matching
