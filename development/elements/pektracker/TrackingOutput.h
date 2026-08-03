/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "Tracker.h"
#include "pek/FrameResults.h"

namespace pek::tracker::trackingoutput {

struct TrackingResult {
    const DetectionTrackAssignments &detectionTrackAssignments;
    const TrackIdList &predictedOnlyTrackIds;
};

void appendTrackingPayloads(perception::FrameResults &frameResults,
                            const DetectionBatch &detections,
                            const ActiveTrackMap &activeTracks,
                            const Config &config,
                            const TrackingResult &trackingResult);

} // namespace pek::tracker::trackingoutput
