/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "Matching.h"

#include "Identity.h"

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

IdentityIdList collectActiveIdentityIds(const ActiveIdentityMap &activeTracks) {
    IdentityIdList identityIds;
    identityIds.reserve(activeTracks.size());
    for (const auto &[identityId, _] : activeTracks) {
        identityIds.push_back(identityId);
    }
    return identityIds;
}

std::vector<amp::Perception::Rect> predictIdentityBoxes(ActiveIdentityMap &activeTracks,
                                                        const Config &config) {
    std::vector<amp::Perception::Rect> predictedIdentityBoxes;
    predictedIdentityBoxes.reserve(activeTracks.size());

    for (auto &[identityId, identity] : activeTracks) {
        (void)identityId;
        const auto predictedPoint = identity::predictCenter(identity, config);
        identity.predictedThisFrame = true;

        auto predictedRect = identity.lastDetection;
        predictedRect.x = predictedPoint.x - (predictedRect.width * 0.5f);
        predictedRect.y = predictedPoint.y - (predictedRect.height * 0.5f);
        predictedIdentityBoxes.push_back(predictedRect);
    }

    return predictedIdentityBoxes;
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
                                            IdentityId identityId,
                                            const EmbeddingBatch &embeddings,
                                            const ActiveIdentityMap &activeTracks,
                                            const Config &config) {
    const auto *embedding = findDetectionEmbedding(detectionIndex, detections, embeddings);
    const auto trackIt = activeTracks.find(identityId);
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
                              const IdentityIdList &identityIds,
                              const std::vector<amp::Perception::Rect> &predictedIdentityBoxes,
                              const EmbeddingBatch &embeddings,
                              const ActiveIdentityMap &activeTracks,
                              const Config &config,
                              CostMatrix &iouMatrix,
                              CostMatrix &costMatrix,
                              SimilarityMatrix &similarityMatrix) {
    iouMatrix.assign(detections.size(), std::vector<float>(identityIds.size(), 0.0f));
    costMatrix.assign(detections.size(), std::vector<float>(identityIds.size(), 1.0f));
    similarityMatrix.assign(detections.size(),
                            std::vector<std::optional<float>>(identityIds.size(), std::nullopt));

    for (size_t detIdx = 0; detIdx < detections.size(); ++detIdx) {
        const auto &det = detections[detIdx];
        for (size_t identityIdx = 0; identityIdx < identityIds.size(); ++identityIdx) {
            const float iou = amp::algo::computeIoU(det, predictedIdentityBoxes[identityIdx]);
            iouMatrix[detIdx][identityIdx] = iou;
            const auto similarity = computeValidSimilarity(
                detIdx, detections, identityIds[identityIdx], embeddings, activeTracks, config);
            similarityMatrix[detIdx][identityIdx] = similarity;
            costMatrix[detIdx][identityIdx] = computeAssociationCost(iou, similarity, config);
        }
    }
}

void collectMatchesFromAssignment(const DetectionBatch &detections,
                                  const std::vector<int> &assignment,
                                  const IdentityIdList &identityIds,
                                  const CostMatrix &iouMatrix,
                                  const SimilarityMatrix &similarityMatrix,
                                  const Config &config,
                                  AssociationResult &result) {
    std::vector<bool> matchedDetection(detections.size(), false);

    for (size_t detIdx = 0; detIdx < assignment.size() && detIdx < detections.size(); ++detIdx) {
        const int identityIdx = assignment[detIdx];
        if (identityIdx < 0 || static_cast<size_t>(identityIdx) >= identityIds.size()) {
            continue;
        }

        const float iou = iouMatrix[detIdx][static_cast<size_t>(identityIdx)];
        if (iou < config.iouThreshold) {
            continue;
        }

        const IdentityId matchedIdentityId = identityIds[static_cast<size_t>(identityIdx)];
        result.matches.push_back({detIdx, matchedIdentityId});
        result.diagnosticsByDetection[detIdx] =
            buildMatchDiagnostic(iou, similarityMatrix[detIdx][static_cast<size_t>(identityIdx)]);
        matchedDetection[detIdx] = true;
    }

    for (size_t detIdx = 0; detIdx < detections.size(); ++detIdx) {
        if (!matchedDetection[detIdx]) {
            result.unmatchedDetections.push_back(detIdx);
        }
    }
}

} // namespace

void clearIdentityPredictionFlags(ActiveIdentityMap &activeTracks) {
    for (auto &[identityId, identity] : activeTracks) {
        (void)identityId;
        identity::clearPredictionFlag(identity);
    }
}

AssociationResult associateDetectionsToActiveIdentities(const DetectionBatch &detections,
                                                        const EmbeddingBatch &embeddings,
                                                        ActiveIdentityMap &activeTracks,
                                                        const Config &config) {
    AssociationResult result;

    if (detections.empty()) {
        return result;
    }

    if (activeTracks.empty()) {
        markAllDetectionsUnmatched(detections.size(), result);
        return result;
    }

    const auto identityIds = collectActiveIdentityIds(activeTracks);
    const auto predictedIdentityBoxes = predictIdentityBoxes(activeTracks, config);
    CostMatrix iouMatrix;
    CostMatrix costMatrix;
    SimilarityMatrix similarityMatrix;
    buildAssociationMatrices(detections,
                             identityIds,
                             predictedIdentityBoxes,
                             embeddings,
                             activeTracks,
                             config,
                             iouMatrix,
                             costMatrix,
                             similarityMatrix);

    const auto assignment = amp::algo::solveHungarian(costMatrix);

    collectMatchesFromAssignment(
        detections, assignment, identityIds, iouMatrix, similarityMatrix, config, result);

    return result;
}

} // namespace amp::tracker::matching
