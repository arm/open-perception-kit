/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "Tracker.h"
#include "opk/FrameResults.h"

namespace opk::tracker::trackingoutput {

struct TrackingResult {
    const DetectionTrackAssignments &detectionTrackAssignments;
    const TrackIdList &predictedOnlyTrackIds;
};

void appendTrackingPayloads(open_perception_kit::FrameResults &frameResults,
                            const DetectionBatch &detections,
                            const ActiveTrackMap &activeTracks,
                            const Config &config,
                            const TrackingResult &trackingResult);

} // namespace opk::tracker::trackingoutput
