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
          const std::vector<float> &embedding) {
    opk::tracker::TrackState track;
    track.trackId = trackId;
    track.lastDetection = lastDetection;
    track.lastEmbedding = embedding;
    track.hasEmbedding = true;
    return track;
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
    opk::tracker::trackstate::appendTracePoint(track, point, config.kalmanDtFallback, config);
    opk::tracker::trackstate::appendTracePoint(track, point, config.kalmanDtFallback, config);
    EXPECT_EQ(track.traceHistoryPoints.size(), 2U);

    config.traceHistorySeconds = 0.0f;
    opk::tracker::trackstate::appendTracePoint(track, point, config.kalmanDtFallback, config);
    EXPECT_EQ(track.traceHistoryPoints.size(), 1U);
}

TEST(OpkTrackerMatching, HybridRejectsNonOverlappingEmbeddingOnlyAssignment) {
    const std::vector<float> embedding{1.0f, 0.0f};
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
    const std::vector<float> embedding{1.0f, 0.0f};
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
    const std::vector<float> embedding{1.0f, 0.0f};
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

TEST(OpkTrackerRuntime, TuningPreservesIdentitiesAndContextChangesClearHistory) {
    opk::tracker::Tracker tracker;
    opk::tracker::Config config;
    config.minHitsToConfirm = 1;
    config.useKalman = false;
    config.useEmbeddings = false;
    const auto nextTrackId = [&] {
        open_perception_kit::FrameResults frameResults;
        open_perception_kit::metadata::BoxDetectionsT detections;
        detections.layer = open_perception_kit::makeLayerInfo({.contentType = config.contentType});
        detections.detections.push_back(
            std::make_unique<open_perception_kit::metadata::BoxDetectionT>(
                makeDetection(1, 0.0f, 0.0f, 10.0f, 10.0f)));
        frameResults.add(std::move(detections));
        tracker.process(frameResults, config, std::nullopt);
        uint64_t trackId = 0;
        frameResults.for_each<open_perception_kit::metadata::ObjectTracksT>(
            [&](const auto &tracks) {
                EXPECT_EQ(tracks.tracks.size(), 1U);
                if (!tracks.tracks.empty()) {
                    trackId = tracks.tracks.front()->track_id;
                }
            });
        return trackId;
    };
    const auto firstId = nextTrackId();
    ASSERT_NE(firstId, 0U);
    config.iouThreshold = 0.7f;
    config.associationMode = opk::tracker::AssociationMode::Iou;
    EXPECT_EQ(nextTrackId(), firstId);
    config.contentType = "humanFace";
    const auto newSourceId = nextTrackId();
    EXPECT_GT(newSourceId, firstId);
    EXPECT_EQ(nextTrackId(), newSourceId);
    config.useKalman = true;
    const auto kalmanId = nextTrackId();
    EXPECT_GT(kalmanId, newSourceId);
    config.inferId = "new-producer";
    const auto newProducerId = nextTrackId();
    EXPECT_GT(newProducerId, kalmanId);
    tracker.clearHistory();
    EXPECT_GT(nextTrackId(), newProducerId);
    tracker.reset();
    EXPECT_EQ(nextTrackId(), firstId);
}
