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

#include <gtest/gtest.h>

#include "Matching.h"
#include "TrackLifecycle.h"
#include "opk/FrameResults.h"
#include "tracking_test_helpers.h"

#include <limits>
#include <vector>

namespace {

open_perception_kit::metadata::BoxDetectionT makeDetection(const uint64_t objectId,
                                                           const float xCoordinate,
                                                           const float yCoordinate,
                                                           const float width,
                                                           const float height) {
    open_perception_kit::metadata::BoxDetectionT detection;
    detection.object = open_perception_kit::makeObjectMeta(objectId);
    detection.box = open_perception_kit::makeBoundingBox(xCoordinate, yCoordinate, width, height);
    detection.confidence = 1.0f;
    return detection;
}

opk::tracker::TrackState
makeTrack(opk::tracker::TrackId trackId,
          const open_perception_kit::metadata::BoxDetectionT &lastDetection,
          const open_perception_kit::metadata::ObjectEmbeddingT &embedding) {
    opk::tracker::TrackState track;
    track.trackId = trackId;
    track.lastDetection = lastDetection;
    track.lastEmbedding = embedding;
    return track;
}

open_perception_kit::metadata::ObjectEmbeddingT makeEmbedding() {
    open_perception_kit::metadata::ObjectEmbeddingT result;
    result.values = {1, 0};
    result.space = std::make_unique<open_perception_kit::metadata::EmbeddingSpaceT>();
    result.space->id = "test:matching:v1";
    result.space->dimensions = 2;
    result.space->metric = open_perception_kit::metadata::EmbeddingMetric::Cosine;
    return result;
}

opk::tracker::Config matchingConfig(opk::tracker::AssociationMode mode) {
    opk::tracker::Config config;
    config.associationMode = mode;
    config.useKalman = false;
    config.iouThreshold = 0.3f;
    config.minCosineSimilarity = 0.0f;
    return config;
}

} // namespace

TEST(OpkTrackerTiming, LargeTraceDurationRetainsPointsAndShrinkingTrimsThem) {
    opk::tracker::TrackState track;
    opk::tracker::Config config;
    config.traceHistorySeconds = std::numeric_limits<float>::max();
    const opk::tracker::Point2f point;
    opk::tracker::trackstate::appendTracePoint(track, point, *testTrackingSample(1), config);
    opk::tracker::trackstate::appendTracePoint(track, point, *testTrackingSample(2), config);
    EXPECT_EQ(track.traceHistoryPoints.size(), 2U);

    config.traceHistorySeconds = 0.0f;
    opk::tracker::trackstate::appendTracePoint(track, point, *testTrackingSample(3), config);
    EXPECT_EQ(track.traceHistoryPoints.size(), 1U);
}

TEST(OpkTrackerMatching, HybridRejectsNonOverlappingEmbeddingOnlyAssignment) {
    const auto embedding = makeEmbedding();
    const auto trackRect = makeDetection(1, 0.0f, 0.0f, 10.0f, 10.0f);
    const auto detectionRect = makeDetection(2, 100.0f, 100.0f, 10.0f, 10.0f);

    opk::tracker::ActiveTrackMap activeTracks;
    activeTracks.emplace(42, makeTrack(42, trackRect, embedding));

    opk::tracker::DetectionBatch detections{&detectionRect};
    opk::tracker::EmbeddingBatch embeddings{{detectionRect.object->id, &embedding}};

    const auto result = opk::tracker::matching::associateDetectionsToActiveTracks(
        detections,
        embeddings,
        activeTracks,
        1.0f / 30.0f,
        matchingConfig(opk::tracker::AssociationMode::Hybrid));

    EXPECT_TRUE(result.matches.empty());
    ASSERT_EQ(result.unmatchedDetections.size(), 1U);
    EXPECT_EQ(result.unmatchedDetections[0], 0U);
}

TEST(OpkTrackerMatching, HybridAcceptsAssignmentWhenIoUPassesThreshold) {
    const auto embedding = makeEmbedding();
    const auto trackRect = makeDetection(1, 0.0f, 0.0f, 10.0f, 10.0f);
    const auto detectionRect = makeDetection(2, 1.0f, 1.0f, 10.0f, 10.0f);

    opk::tracker::ActiveTrackMap activeTracks;
    activeTracks.emplace(42, makeTrack(42, trackRect, embedding));

    opk::tracker::DetectionBatch detections{&detectionRect};
    opk::tracker::EmbeddingBatch embeddings{{detectionRect.object->id, &embedding}};

    const auto result = opk::tracker::matching::associateDetectionsToActiveTracks(
        detections,
        embeddings,
        activeTracks,
        1.0f / 30.0f,
        matchingConfig(opk::tracker::AssociationMode::Hybrid));

    ASSERT_EQ(result.matches.size(), 1U);
    EXPECT_EQ(result.matches[0].first, 0U);
    EXPECT_EQ(result.matches[0].second, 42U);
    EXPECT_TRUE(result.unmatchedDetections.empty());
}

TEST(OpkTrackerMatching, EmbeddingModeAcceptsNonOverlappingEmbeddingAssignment) {
    const auto embedding = makeEmbedding();
    const auto trackRect = makeDetection(1, 0.0f, 0.0f, 10.0f, 10.0f);
    const auto detectionRect = makeDetection(2, 100.0f, 100.0f, 10.0f, 10.0f);

    opk::tracker::ActiveTrackMap activeTracks;
    activeTracks.emplace(42, makeTrack(42, trackRect, embedding));

    opk::tracker::DetectionBatch detections{&detectionRect};
    opk::tracker::EmbeddingBatch embeddings{{detectionRect.object->id, &embedding}};

    const auto result = opk::tracker::matching::associateDetectionsToActiveTracks(
        detections,
        embeddings,
        activeTracks,
        1.0f / 30.0f,
        matchingConfig(opk::tracker::AssociationMode::Embedding));

    ASSERT_EQ(result.matches.size(), 1U);
    EXPECT_EQ(result.matches[0].first, 0U);
    EXPECT_EQ(result.matches[0].second, 42U);
    EXPECT_TRUE(result.unmatchedDetections.empty());
}

TEST(OpkTrackerMatching, IncompatibleEmbeddingsCannotStealMinimumCosineAssignments) {
    const auto trackA = makeEmbedding();
    auto trackB = makeEmbedding();
    trackB.space->id = "test:other-space";
    auto detectionA = trackA;
    detectionA.values = {-1, 0};
    auto detectionB = trackB;
    detectionB.values = {-1, 0};
    const auto first = makeDetection(1, 0, 0, 10, 10);
    const auto second = makeDetection(2, 20, 0, 10, 10);
    auto config = matchingConfig(opk::tracker::AssociationMode::Embedding);
    config.minCosineSimilarity = -1;
    config.useEmbeddings = true;

    // Crossed spaces force compatible pairs off the solver's default diagonal.
    // Rectangular cases exercise both assignment orientations and missing vectors.
    for (const auto &dimensions : {std::pair{2, 2}, std::pair{2, 1}, std::pair{1, 2}}) {
        for (const bool missing : {false, true}) {
            SCOPED_TRACE(dimensions.first);
            SCOPED_TRACE(dimensions.second);
            SCOPED_TRACE(missing);
            opk::tracker::ActiveTrackMap tracks;
            tracks.emplace(42, makeTrack(42, first, trackA));
            if (dimensions.second == 2)
                tracks.emplace(43, makeTrack(43, second, trackB));
            opk::tracker::DetectionBatch detections{&first};
            if (dimensions.first == 2)
                detections.push_back(&second);
            opk::tracker::EmbeddingBatch embeddings{{2, &detectionA}};
            if (!missing)
                embeddings.emplace(1, &detectionB);
            const auto result = opk::tracker::matching::associateDetectionsToActiveTracks(
                detections, embeddings, tracks, 1.0f / 30.0f, config);
            std::vector<std::pair<size_t, opk::tracker::TrackId>> expected;
            if (dimensions.second == 2 && !missing)
                expected.emplace_back(0, 43);
            if (dimensions.first == 2)
                expected.emplace_back(1, 42);
            EXPECT_EQ(result.matches, expected);
            EXPECT_EQ(result.unmatchedDetections.size(), detections.size() - expected.size());
            for (const auto &[index, track] : expected) {
                const auto association = result.associationsByDetection.find(index);
                ASSERT_NE(association, result.associationsByDetection.end());
                EXPECT_EQ(association->second.score, -1.0f);
            }
        }
    }
}

TEST(OpkTrackerMatching, DiagnosticKindDoesNotClaimTheConfiguredMatchingStrategy) {
    const auto embedding = makeEmbedding();
    const auto before = makeDetection(1, 0, 0, 10, 10);
    const auto after = makeDetection(2, 1, 1, 10, 10);
    opk::tracker::ActiveTrackMap tracks;
    tracks.emplace(42, makeTrack(42, before, embedding));
    const opk::tracker::DetectionBatch detections{&after};
    const opk::tracker::EmbeddingBatch embeddings{{2, &embedding}};
    const auto result = opk::tracker::matching::associateDetectionsToActiveTracks(
        detections,
        embeddings,
        tracks,
        1.0f / 30.0f,
        matchingConfig(opk::tracker::AssociationMode::Iou));
    ASSERT_EQ(result.matches.size(), 1U);
    EXPECT_EQ(result.associationsByDetection.at(0).kind,
              open_perception_kit::metadata::TrackAssociationKind::CosineScore);
}

TEST(OpkTrackerTiming, KalmanDtUsesRunningTimeDeltaUnlessFallbackIsForced) {
    opk::tracker::Config config;
    opk::tracker::KalmanDeltaTimeTracking timing;

    timing.update(1'000ULL, config);
    EXPECT_TRUE(timing.usesFallback());

    timing.update(1'250ULL, config);
    EXPECT_FLOAT_EQ(timing.effectiveKalmanDt(), 0.25f);
    EXPECT_DOUBLE_EQ(timing.trackerTimeMs(),
                     static_cast<double>(config.kalmanDtFallback) * 1'000.0 + 250.0);
    EXPECT_FALSE(timing.usesFallback());

    config.kalmanDtFallback = 0.1f;
    config.kalmanDtForceFallback = true;
    timing.update(2'000ULL, config);
    EXPECT_FLOAT_EQ(timing.effectiveKalmanDt(), 0.1f);
    EXPECT_DOUBLE_EQ(timing.trackerTimeMs(),
                     static_cast<double>(opk::tracker::Defaults::kalmanDtFallback) * 1'000.0 +
                         1'000.0);
    EXPECT_TRUE(timing.usesFallback());
    EXPECT_TRUE(timing.fallbackForced());
}

TEST(OpkTrackerTiming, KalmanDtResynchronizesAfterMissingRunningTime) {
    opk::tracker::Config config;
    opk::tracker::KalmanDeltaTimeTracking timing;

    timing.update(0ULL, config);
    timing.update(std::nullopt, config);
    timing.update(66ULL, config);
    EXPECT_FLOAT_EQ(timing.effectiveKalmanDt(), config.kalmanDtFallback);
    EXPECT_DOUBLE_EQ(timing.trackerTimeMs(),
                     static_cast<double>(config.kalmanDtFallback) * 3'000.0);
    EXPECT_TRUE(timing.usesFallback());

    timing.update(99ULL, config);
    EXPECT_FLOAT_EQ(timing.effectiveKalmanDt(), 0.033f);
    EXPECT_DOUBLE_EQ(timing.trackerTimeMs(),
                     static_cast<double>(config.kalmanDtFallback) * 3'000.0 + 33.0);
    EXPECT_FALSE(timing.usesFallback());
}

TEST(OpkTrackerTiming, DormantTrackExpiresUsingTrackerTime) {
    opk::tracker::DetectionBatch detections;
    opk::tracker::EmbeddingBatch embeddings;
    opk::tracker::AssociationResult association;
    opk::tracker::Config config;
    config.dormantTrackHistorySeconds = 8.0f;

    opk::tracker::ActiveTrackMap activeTracks;
    opk::tracker::DormantTrackMap inactiveTracks;
    opk::tracker::DormantTrackState dormantTrack;
    dormantTrack.trackId = 1;
    dormantTrack.storedAtTrackerTimeMs = 1'000.0;
    const bool inserted =
        inactiveTracks.try_emplace(dormantTrack.trackId, std::move(dormantTrack)).second;
    ASSERT_TRUE(inserted);
    opk::tracker::TrackId nextTrackId = 2;

    const auto frameTrackingContext = opk::tracker::tracklifecycle::FrameTrackingContext{
        detections, embeddings, association, 11, 11'000ULL, 10.0f, config};
    auto mutableTrackState =
        opk::tracker::tracklifecycle::MutableTrackState{activeTracks, inactiveTracks, nextTrackId};

    opk::tracker::tracklifecycle::expireInactiveTracks(frameTrackingContext, mutableTrackState);

    EXPECT_TRUE(inactiveTracks.empty());
}
