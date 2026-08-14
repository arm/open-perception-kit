/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include "Matching.h"
#include "pek/FrameResults.h"

#include <vector>

namespace {

perception::metadata::BoxDetectionT
makeDetection(uint64_t id, float x, float y, float width, float height) {
    perception::metadata::BoxDetectionT detection;
    detection.object = perception::makeObjectMeta(id);
    detection.box = perception::makeBoundingBox(x, y, width, height);
    detection.confidence = 1.0f;
    return detection;
}

pek::tracker::TrackState makeTrack(pek::tracker::TrackId trackId,
                                   const perception::metadata::BoxDetectionT &lastDetection,
                                   const std::vector<float> &embedding) {
    pek::tracker::TrackState track;
    track.trackId = trackId;
    track.lastDetection = lastDetection;
    track.lastEmbedding = embedding;
    track.hasEmbedding = true;
    return track;
}

pek::tracker::Config matchingConfig(pek::tracker::AssociationMode mode) {
    pek::tracker::Config config;
    config.associationMode = mode;
    config.useKalman = false;
    config.iouThreshold = 0.3f;
    config.minCosineSimilarity = 0.0f;
    return config;
}

} // namespace

TEST(PekTrackerMatching, HybridRejectsNonOverlappingEmbeddingOnlyAssignment) {
    const std::vector<float> embedding{1.0f, 0.0f};
    const auto trackRect = makeDetection(1, 0.0f, 0.0f, 10.0f, 10.0f);
    const auto detectionRect = makeDetection(2, 100.0f, 100.0f, 10.0f, 10.0f);

    pek::tracker::ActiveTrackMap activeTracks;
    activeTracks.emplace(42, makeTrack(42, trackRect, embedding));

    pek::tracker::DetectionBatch detections{&detectionRect};
    pek::tracker::EmbeddingBatch embeddings{{detectionRect.object->id, &embedding}};

    const auto result = pek::tracker::matching::associateDetectionsToActiveTracks(
        detections,
        embeddings,
        activeTracks,
        1.0f / 30.0f,
        matchingConfig(pek::tracker::AssociationMode::Hybrid));

    EXPECT_TRUE(result.matches.empty());
    ASSERT_EQ(result.unmatchedDetections.size(), 1U);
    EXPECT_EQ(result.unmatchedDetections[0], 0U);
}

TEST(PekTrackerMatching, HybridAcceptsAssignmentWhenIoUPassesThreshold) {
    const std::vector<float> embedding{1.0f, 0.0f};
    const auto trackRect = makeDetection(1, 0.0f, 0.0f, 10.0f, 10.0f);
    const auto detectionRect = makeDetection(2, 1.0f, 1.0f, 10.0f, 10.0f);

    pek::tracker::ActiveTrackMap activeTracks;
    activeTracks.emplace(42, makeTrack(42, trackRect, embedding));

    pek::tracker::DetectionBatch detections{&detectionRect};
    pek::tracker::EmbeddingBatch embeddings{{detectionRect.object->id, &embedding}};

    const auto result = pek::tracker::matching::associateDetectionsToActiveTracks(
        detections,
        embeddings,
        activeTracks,
        1.0f / 30.0f,
        matchingConfig(pek::tracker::AssociationMode::Hybrid));

    ASSERT_EQ(result.matches.size(), 1U);
    EXPECT_EQ(result.matches[0].first, 0U);
    EXPECT_EQ(result.matches[0].second, 42U);
    EXPECT_TRUE(result.unmatchedDetections.empty());
}

TEST(PekTrackerMatching, EmbeddingModeAcceptsNonOverlappingEmbeddingAssignment) {
    const std::vector<float> embedding{1.0f, 0.0f};
    const auto trackRect = makeDetection(1, 0.0f, 0.0f, 10.0f, 10.0f);
    const auto detectionRect = makeDetection(2, 100.0f, 100.0f, 10.0f, 10.0f);

    pek::tracker::ActiveTrackMap activeTracks;
    activeTracks.emplace(42, makeTrack(42, trackRect, embedding));

    pek::tracker::DetectionBatch detections{&detectionRect};
    pek::tracker::EmbeddingBatch embeddings{{detectionRect.object->id, &embedding}};

    const auto result = pek::tracker::matching::associateDetectionsToActiveTracks(
        detections,
        embeddings,
        activeTracks,
        1.0f / 30.0f,
        matchingConfig(pek::tracker::AssociationMode::Embedding));

    ASSERT_EQ(result.matches.size(), 1U);
    EXPECT_EQ(result.matches[0].first, 0U);
    EXPECT_EQ(result.matches[0].second, 42U);
    EXPECT_TRUE(result.unmatchedDetections.empty());
}

TEST(PekTrackerTiming, KalmanDtUsesRunningTimeDeltaUnlessFallbackIsForced) {
    pek::tracker::Config config;
    pek::tracker::KalmanDeltaTimeTracking timing;

    timing.update(1'000ULL, config);
    EXPECT_TRUE(timing.usesFallback());

    timing.update(1'250ULL, config);
    EXPECT_FLOAT_EQ(timing.effectiveKalmanDt(), 0.25f);
    EXPECT_FALSE(timing.usesFallback());

    config.kalmanDtFallback = 0.1f;
    config.kalmanDtForceFallback = true;
    timing.update(2'000ULL, config);
    EXPECT_FLOAT_EQ(timing.effectiveKalmanDt(), 0.1f);
    EXPECT_TRUE(timing.usesFallback());
    EXPECT_TRUE(timing.fallbackForced());
}

TEST(PekTrackerTiming, KalmanDtResynchronizesAfterMissingRunningTime) {
    pek::tracker::Config config;
    pek::tracker::KalmanDeltaTimeTracking timing;

    timing.update(0ULL, config);
    timing.update(std::nullopt, config);
    timing.update(66ULL, config);
    EXPECT_FLOAT_EQ(timing.effectiveKalmanDt(), config.kalmanDtFallback);
    EXPECT_TRUE(timing.usesFallback());

    timing.update(99ULL, config);
    EXPECT_FLOAT_EQ(timing.effectiveKalmanDt(), 0.033f);
    EXPECT_FALSE(timing.usesFallback());
}
