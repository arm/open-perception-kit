/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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
#include "Tracker.h"
#include "tracking_test_helpers.h"
#include <gtest/gtest.h>

namespace {
using namespace open_perception_kit;
using namespace open_perception_kit::metadata;

FrameResults observed(const uint64_t id, const std::string &text = "person") {
    FrameResults result;
    BoxDetectionsT boxes;
    boxes.layer = makeLayerInfo({.contentType = "genericObject"});
    auto box = std::make_unique<BoxDetectionT>();
    box->object = makeObjectMeta(id);
    box->box = makeImageBoundingBox(
        {.width = 100, .height = 100, .frameId = 1, .frameWidth = 100, .frameHeight = 100},
        10,
        10,
        20,
        20);
    box->text = text;
    box->confidence = .75f;
    boxes.detections.push_back(std::move(box));
    result.add(std::move(boxes));
    return result;
}
ObjectTrackT onlyTrack(const FrameResults &frame) {
    ObjectTrackT result;
    size_t count = 0;
    frame.for_each<ObjectTracksT>([&](const auto &payload) {
        EXPECT_TRUE(payload.layer->tags.empty());
        for (const auto &track : payload.tracks) {
            result = *track;
            ++count;
        }
    });
    EXPECT_EQ(count, 1U);
    EXPECT_TRUE(opk::tracking::validTrack(result));
    return result;
}
opk::tracker::Config config() {
    opk::tracker::Config value;
    value.useKalman = false;
    value.useEmbeddings = false;
    value.minHitsToConfirm = 1;
    value.inferId = "infer";
    value.producerInstanceId = "tracker";
    return value;
}

TEST(TrackingContract, ObservationsPredictionAndHistoryAreExplicitWithoutDisplayMutation) {
    opk::tracker::Tracker tracker;
    const auto settings = config();
    auto first = observed(1000000);
    tracker.process(first, settings, 0);
    const auto initial = onlyTrack(first);
    EXPECT_NE(initial.object->id, initial.source_id.value_or(0));
    EXPECT_EQ(initial.text, "person");
    EXPECT_EQ(opk::tracking::label(initial, true), "person [ID:1 NEW]");
    EXPECT_EQ(opk::tracking::label(initial, false), "person");
    EXPECT_EQ(initial.sample->state, TrackObservationState::Observed);
    EXPECT_EQ(initial.sample->time_source, TrackingTimeSource::FallbackDelta);
    EXPECT_EQ(initial.source_frame_index, 1U);
    FrameResults missing;
    tracker.process(missing, settings, 100);
    const auto predicted = onlyTrack(missing);
    EXPECT_EQ(predicted.identity->session_id, initial.identity->session_id);
    EXPECT_EQ(predicted.source_id, initial.source_id);
    EXPECT_EQ(predicted.source_frame_index, 1U);
    EXPECT_EQ(predicted.sample->frame_index, 2U);
    EXPECT_EQ(predicted.sample->state, TrackObservationState::Predicted);
    EXPECT_FALSE(predicted.association);
    EXPECT_EQ(opk::tracking::label(predicted, true), "person [ID:1 PRED]");
    EXPECT_EQ(missing.count<BoxDetectionsT>(), 0U);
    missing.for_each<TrackTracesT>([&](const auto &payload) {
        ASSERT_EQ(payload.traces.size(), 1U);
        const auto &trace = *payload.traces[0];
        EXPECT_TRUE(opk::tracking::validTrace(trace));
        EXPECT_EQ(trace.identity->session_id, predicted.identity->session_id);
        ASSERT_EQ(trace.points.size(), 2U);
        EXPECT_EQ(trace.points[0]->sample->state, TrackObservationState::Observed);
        EXPECT_EQ(trace.points[1]->sample->state, TrackObservationState::Predicted);
    });
    auto reacquired = observed(1000001, "renamed");
    tracker.process(reacquired, settings, 200);
    const auto current = onlyTrack(reacquired);
    EXPECT_EQ(current.identity->track_id, initial.identity->track_id);
    EXPECT_EQ(current.source_frame_index, 3U);
    EXPECT_EQ(current.source_id, 1000001U);
    EXPECT_EQ(current.association->kind, TrackAssociationKind::IoUScore);
    EXPECT_EQ(opk::tracking::label(current, true), "renamed [ID:1 IOU:1.00]");
    first.for_each<BoxDetectionsT>(
        [](const auto &payload) { EXPECT_EQ(payload.detections[0]->text, "person"); });
}

TEST(TrackingContract, MultipleTrackersAndRestartHaveSeparateIdentityScopes) {
    opk::tracker::Tracker a, b;
    const auto settings = config();
    auto one = observed(1000000), two = observed(1000001);
    a.process(one, settings, 0);
    b.process(two, settings, 0);
    const auto first = onlyTrack(one), second = onlyTrack(two);
    EXPECT_EQ(first.identity->track_id, second.identity->track_id);
    EXPECT_NE(first.identity->session_id, second.identity->session_id);
    a.reset();
    auto restarted = observed(1000002);
    a.process(restarted, settings, 0);
    const auto third = onlyTrack(restarted);
    EXPECT_EQ(third.identity->track_id, 1U);
    EXPECT_EQ(third.sample->frame_index, 1U);
    EXPECT_NE(third.identity->session_id, first.identity->session_id);
}

TEST(TrackingContract, PredictionHistoryCanStandAloneWhenPredictedBoxesAreDisabled) {
    opk::tracker::Tracker tracker;
    auto settings = config();
    settings.emitPredictedDetections = false;
    auto first = observed(1000000);
    tracker.process(first, settings, 0);
    FrameResults prediction;
    tracker.process(prediction, settings, 100);
    ASSERT_EQ(prediction.count<ObjectTracksT>(), 1U);
    prediction.for_each<ObjectTracksT>(
        [](const auto &payload) { EXPECT_TRUE(payload.tracks.empty()); });
    ASSERT_EQ(prediction.count<TrackTracesT>(), 1U);
    prediction.for_each<TrackTracesT>([](const auto &payload) {
        EXPECT_TRUE(opk::tracking::validTrace(*payload.traces[0]));
        EXPECT_EQ(payload.traces[0]->points.back()->sample->state,
                  TrackObservationState::Predicted);
    });
}

TEST(TrackingContract, DormantReacquisitionKeepsIdentityAndStartsANewTraceSegment) {
    opk::tracker::Tracker tracker;
    auto settings = config();
    settings.useEmbeddings = true;
    settings.maxMissedFrames = 0;
    settings.associationMode = opk::tracker::AssociationMode::Embedding;
    const auto withEmbedding = [](const uint64_t id) {
        auto result = observed(id);
        ObjectEmbeddingsT payload;
        auto value = std::make_unique<ObjectEmbeddingT>();
        value->subject_id = id;
        value->values = {1, 0};
        value->space = std::make_unique<EmbeddingSpaceT>();
        value->space->id = "test:tracking";
        value->space->dimensions = 2;
        value->space->metric = EmbeddingMetric::Cosine;
        payload.embeddings.push_back(std::move(value));
        result.add(std::move(payload));
        return result;
    };
    auto first = withEmbedding(1000000);
    tracker.process(first, settings, 0);
    const auto initial = onlyTrack(first);
    FrameResults gap;
    tracker.process(gap, settings, 100);
    auto next = withEmbedding(1000001);
    tracker.process(next, settings, 200);
    const auto restored = onlyTrack(next);
    EXPECT_EQ(restored.identity->session_id, initial.identity->session_id);
    EXPECT_EQ(restored.identity->track_id, initial.identity->track_id);
    EXPECT_EQ(restored.association->kind, TrackAssociationKind::Reacquired);
    EXPECT_EQ(restored.sample->state, TrackObservationState::Observed);
    EXPECT_EQ(restored.source_frame_index, 3U);
    ASSERT_EQ(next.count<TrackTracesT>(), 1U);
    next.for_each<TrackTracesT>([](const auto &payload) { EXPECT_TRUE(payload.traces.empty()); });
}

TEST(TrackingContract, EmptyUnconfirmedAndExpiredResultsRemainExplicit) {
    opk::tracker::Tracker tracker;
    auto settings = config();
    settings.minHitsToConfirm = 2;
    settings.maxMissedFrames = 0;
    const auto emptyInput = [] {
        FrameResults result;
        BoxDetectionsT detections;
        detections.layer = makeLayerInfo({.contentType = "genericObject"});
        result.add(std::move(detections));
        return result;
    };
    const auto expectEmpty = [](const FrameResults &result, const bool tracesEnabled) {
        const FrameResults decoded(serialize(result));
        ASSERT_EQ(decoded.count<ObjectTracksT>(), 1U);
        decoded.for_each<ObjectTracksT>([](const auto &payload) {
            ASSERT_TRUE(payload.layer);
            ASSERT_TRUE(payload.layer->producer);
            EXPECT_EQ(payload.layer->producer->component, "gstreamer/opktracker");
            EXPECT_TRUE(payload.tracks.empty());
        });
        EXPECT_EQ(decoded.count<TrackTracesT>(), tracesEnabled ? 1U : 0U);
        decoded.for_each<TrackTracesT>(
            [](const auto &payload) { EXPECT_TRUE(payload.traces.empty()); });
    };
    auto empty = emptyInput();
    tracker.process(empty, settings, 0);
    expectEmpty(empty, true);
    auto unconfirmed = observed(1000000);
    tracker.process(unconfirmed, settings, 100);
    expectEmpty(unconfirmed, true);
    auto confirmed = observed(1000001);
    tracker.process(confirmed, settings, 200);
    EXPECT_EQ(onlyTrack(confirmed).sample->state, TrackObservationState::Observed);
    auto expired = emptyInput();
    tracker.process(expired, settings, 300);
    expectEmpty(expired, true);
    settings.emitTrace = false;
    auto disabled = emptyInput();
    tracker.process(disabled, settings, 400);
    expectEmpty(disabled, false);
}

TEST(TrackingContract, TraceRetentionUsesElapsedTimeEvenWithForcedKalmanFallback) {
    opk::tracker::Tracker tracker;
    auto settings = config();
    settings.traceHistorySeconds = 1;
    settings.kalmanDtForceFallback = true;
    for (uint64_t time = 0; time <= 900; time += 100) {
        auto frame = observed(1000000 + time);
        tracker.process(frame, settings, time);
    }
    auto latest = observed(2000000);
    tracker.process(latest, settings, 1200);
    ASSERT_EQ(latest.count<TrackTracesT>(), 1U);
    latest.for_each<TrackTracesT>([](const auto &payload) {
        ASSERT_EQ(payload.traces.size(), 1U);
        const auto &trace = *payload.traces[0];
        ASSERT_EQ(trace.points.size(), 8U);
        EXPECT_TRUE(opk::tracking::validTrace(trace));
        EXPECT_EQ(trace.points.front()->sample->frame_index, 4U);
        EXPECT_EQ(trace.points.back()->sample->frame_index, 11U);
        EXPECT_EQ(trace.points.back()->sample->time_source, TrackingTimeSource::MediaDelta);
    });
    auto missingTime = observed(2000001);
    tracker.process(missingTime, settings, std::nullopt);
    EXPECT_EQ(onlyTrack(missingTime).sample->time_source, TrackingTimeSource::FallbackDelta);
}

TEST(TrackingContract, InvalidSourceRelationsAndUnorderedTracesAreNotInterpreted) {
    opk::tracker::Tracker tracker;
    auto frame = observed(1000000);
    tracker.process(frame, config(), 0);
    auto value = onlyTrack(frame);
    value.source_frame_index = 2;
    EXPECT_FALSE(opk::tracking::validTrack(value));
    value.source_frame_index = 1;
    value.sample->state = TrackObservationState::Predicted;
    value.association.reset();
    EXPECT_FALSE(opk::tracking::validTrack(value));
    TrackTraceT trace;
    trace.identity = testTrackIdentity(1);
    trace.space = std::make_unique<ImageSpaceT>(*value.box->space);
    for (const uint64_t index : {1U, 2U}) {
        auto point = std::make_unique<TrackTracePointT>();
        point->sample = testTrackingSample(index);
        trace.points.push_back(std::move(point));
    }
    EXPECT_TRUE(opk::tracking::validTrace(trace));
    trace.points[1]->sample->elapsed_ms = 5;
    EXPECT_FALSE(opk::tracking::validTrace(trace));
}
} // namespace
