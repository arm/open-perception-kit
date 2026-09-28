/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#include <gtest/gtest.h>

#include "TrackingOutput.h"
#include "opk/FrameResults.h"

namespace {

opk::tracker::ActiveTrackMap makeTraceableTracks() {
    opk::tracker::ActiveTrackMap activeTracks;
    auto &track = activeTracks[42U];
    track.trackId = 42U;
    track.hitStreak = 1;
    track.lastDetection.object = open_perception_kit::makeObjectMeta(7U, 3U);
    track.lastDetection.box = open_perception_kit::makeBoundingBox(1.0f, 2.0f, 3.0f, 4.0f);

    open_perception_kit::metadata::Point2fT firstPoint;
    firstPoint.x = 10.0f;
    firstPoint.y = 20.0f;
    track.traceHistoryPoints.push_back(firstPoint);

    open_perception_kit::metadata::Point2fT secondPoint;
    secondPoint.x = 11.0f;
    secondPoint.y = 21.0f;
    track.traceHistoryPoints.push_back(secondPoint);
    return activeTracks;
}

void appendTrackingOutput(open_perception_kit::FrameResults &frameResults, bool emitTrace) {
    const opk::tracker::DetectionBatch detections;
    const auto activeTracks = makeTraceableTracks();
    opk::tracker::Config config;
    config.emitTrace = emitTrace;
    config.minHitsToConfirm = 1;
    config.inferId = "opkinfer0";
    config.producerInstanceId = "tracker-secondary";
    const opk::tracker::DetectionTrackAssignments assignments;
    const opk::tracker::TrackIdList predictedOnlyTrackIds;
    const opk::tracker::trackingoutput::TrackingResult trackingResult{
        assignments,
        predictedOnlyTrackIds,
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
    });
}
