/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include "TrackingOutput.h"
#include "pek/FrameResults.h"

namespace {

pek::tracker::ActiveTrackMap makeTraceableTracks() {
    pek::tracker::ActiveTrackMap activeTracks;
    auto &track = activeTracks[42U];
    track.trackId = 42U;
    track.hitStreak = 1;
    track.lastDetection.object = perception::makeObjectMeta(7U, 3U);
    track.lastDetection.box = perception::makeBoundingBox(1.0f, 2.0f, 3.0f, 4.0f);

    perception::metadata::Point2fT firstPoint;
    firstPoint.x = 10.0f;
    firstPoint.y = 20.0f;
    track.traceHistoryPoints.push_back(firstPoint);

    perception::metadata::Point2fT secondPoint;
    secondPoint.x = 11.0f;
    secondPoint.y = 21.0f;
    track.traceHistoryPoints.push_back(secondPoint);
    return activeTracks;
}

void appendTrackingOutput(perception::FrameResults &frameResults, bool emitTrace) {
    const pek::tracker::DetectionBatch detections;
    const auto activeTracks = makeTraceableTracks();
    pek::tracker::Config config;
    config.emitTrace = emitTrace;
    config.minHitsToConfirm = 1;
    const pek::tracker::DetectionTrackAssignments assignments;
    const pek::tracker::TrackIdList predictedOnlyTrackIds;
    const pek::tracker::trackingoutput::TrackingResult trackingResult{
        assignments,
        predictedOnlyTrackIds,
    };

    pek::tracker::trackingoutput::appendTrackingPayloads(
        frameResults, detections, activeTracks, config, trackingResult);
}

} // namespace

TEST(PekTrackerTrackingOutput, SuppressesTracePayloadWhenDisabled) {
    perception::FrameResults frameResults;

    appendTrackingOutput(frameResults, false);

    EXPECT_EQ(frameResults.count<perception::metadata::TrackTracesT>(), 0U);
}

TEST(PekTrackerTrackingOutput, EmitsTracePayloadWhenEnabled) {
    perception::FrameResults frameResults;

    appendTrackingOutput(frameResults, true);

    EXPECT_EQ(frameResults.count<perception::metadata::TrackTracesT>(), 1U);
}
