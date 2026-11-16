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

#include "TrackingOutput.h"
#include "opk/FrameResults.h"
#include "tracking_test_helpers.h"

namespace {

opk::tracker::ActiveTrackMap
makeTraceableTracks(const flatbuffers::Optional<std::uint32_t> classId = flatbuffers::nullopt) {
    opk::tracker::ActiveTrackMap activeTracks;
    auto &track = activeTracks[42U];
    track.trackId = 42U;
    track.lastObservationFrameIndex = 2;
    track.hitStreak = 1;
    track.lastDetection.object = open_perception_kit::makeObjectMeta(7U, 3U);
    track.lastDetection.class_id = classId;
    track.lastDetection.box = open_perception_kit::makeImageBoundingBox(
        {.width = 100, .height = 100, .frameId = 3, .frameWidth = 100, .frameHeight = 100},
        1.0f,
        2.0f,
        3.0f,
        4.0f);

    open_perception_kit::metadata::TrackTracePointT firstPoint;
    firstPoint.sample = testTrackingSample(1);
    firstPoint.x = 10.0f;
    firstPoint.y = 20.0f;
    track.traceHistoryPoints.push_back(firstPoint);

    open_perception_kit::metadata::TrackTracePointT secondPoint;
    secondPoint.sample = testTrackingSample(2);
    secondPoint.x = 11.0f;
    secondPoint.y = 21.0f;
    track.traceHistoryPoints.push_back(secondPoint);
    return activeTracks;
}

void appendTrackingOutput(
    open_perception_kit::FrameResults &frameResults,
    bool emitTrace,
    const flatbuffers::Optional<std::uint32_t> classId = flatbuffers::nullopt) {
    const auto activeTracks = makeTraceableTracks(classId);
    const opk::tracker::DetectionBatch detections{&activeTracks.at(42U).lastDetection};
    opk::tracker::Config config;
    config.emitTrace = emitTrace;
    config.minHitsToConfirm = 1;
    config.inferId = "opkinfer0";
    config.producerInstanceId = "tracker-secondary";
    const opk::tracker::DetectionTrackAssignments assignments{{0U, 42U}};
    const opk::tracker::TrackIdList predictedOnlyTrackIds;
    const opk::tracker::trackingoutput::TrackingResult trackingResult{
        assignments,
        predictedOnlyTrackIds,
        "test-session",
        *testTrackingSample(2),
    };

    opk::tracker::trackingoutput::appendTrackingPayloads(
        frameResults, detections, activeTracks, config, trackingResult);
}

} // namespace

TEST(OpkTrackerTrackingOutput, SuppressesTracePayloadWhenDisabled) {
    open_perception_kit::FrameResults frameResults;

    appendTrackingOutput(frameResults, false);

    EXPECT_EQ(frameResults.count<open_perception_kit::metadata::TrackTracesT>(), 0U);
}

TEST(OpkTrackerTrackingOutput, PreservesAbsentAndZeroCategoryIds) {
    for (const auto classId : {flatbuffers::Optional<std::uint32_t>{},
                               flatbuffers::Optional<std::uint32_t>{0U},
                               flatbuffers::Optional<std::uint32_t>{7U}}) {
        open_perception_kit::FrameResults results;
        appendTrackingOutput(results, false, classId);
        const open_perception_kit::FrameResults decoded(open_perception_kit::serialize(results));
        EXPECT_EQ(decoded.count<open_perception_kit::metadata::ObjectTracksT>(), 1U);
        decoded.for_each<open_perception_kit::metadata::ObjectTracksT>(
            [classId](const auto &payload) {
                ASSERT_EQ(payload.tracks.size(), 1U);
                EXPECT_EQ(payload.tracks[0]->class_id, classId);
            });
    }
}

TEST(OpkTrackerTrackingOutput, EmitsTracePayloadWhenEnabled) {
    open_perception_kit::FrameResults frameResults;

    appendTrackingOutput(frameResults, true);

    EXPECT_EQ(frameResults.count<open_perception_kit::metadata::TrackTracesT>(), 1U);
    frameResults.for_each<open_perception_kit::metadata::TrackTracesT>([](const auto &payload) {
        ASSERT_NE(payload.layer, nullptr);
        ASSERT_NE(payload.layer->producer, nullptr);
        EXPECT_EQ(payload.layer->producer->instance_id, "opkinfer0/tracker-secondary");
        EXPECT_EQ(payload.layer->producer->component, "gstreamer/opktracker");
        EXPECT_EQ(payload.layer->producer->implementation, "Tracker");
        ASSERT_EQ(payload.traces.size(), 1U);
        ASSERT_NE(payload.traces[0]->space, nullptr);
        EXPECT_EQ(payload.traces[0]->space->frame_id, 3U);
    });
}
