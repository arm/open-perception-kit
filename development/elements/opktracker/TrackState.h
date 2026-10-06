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

#pragma once

#include "opk/FrameResults.h"
#include "opk/KalmanFilter.h"

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace opk::tracker {

struct Config;

struct Point2f {
    float x = 0.0f;
    float y = 0.0f;
};

struct TrackState { // NOSONAR: std::deque move construction is not noexcept.
    using Kalman = opk::KalmanFilter<4, 2, float>;
    uint64_t trackId = 0;
    open_perception_kit::metadata::BoxDetectionT lastDetection;
    std::string lastMatchDiagnostic = "NEW";
    std::deque<open_perception_kit::metadata::Point2fT> traceHistoryPoints;
    bool kalmanInitialized = false;
    bool predictedThisFrame = false;
    Kalman kalman;
    std::vector<float> lastEmbedding;
    bool hasEmbedding = false;
    int missedFrames = 0;
    int hitStreak = 0;
    uint64_t lastUpdateFrame = 0;
};

struct DormantTrackState {
    uint64_t trackId = 0;
    open_perception_kit::metadata::BoxDetectionT lastDetection;
    std::vector<float> lastEmbedding;
    double storedAtTrackerTimeMs = 0.0;
};

} // namespace opk::tracker

namespace opk::tracker::trackstate {

/**
 * @brief Reset the per-frame prediction marker for a track.
 * @param track Track state to update.
 * @return None.
 */
void clearPredictionFlag(TrackState &track);

/**
 * @brief Run prediction step and return the predicted center point.
 * @param track Track state whose motion state is predicted.
 * @param config Tracker configuration used by prediction logic.
 * @return Predicted center point of the track.
 */
Point2f predictCenter(TrackState &track, float kalmanDt, const Config &config);

/**
 * @brief Correct track state using the latest detection measurement.
 * @param track Track state whose state is corrected.
 * @param detection Detection rectangle used as measurement.
 * @param config Tracker configuration used by correction logic.
 * @return Corrected/smoothed center point after measurement update.
 */
Point2f correctCenterWithMeasurement(TrackState &track,
                                     const open_perception_kit::metadata::BoxDetectionT &detection,
                                     float kalmanDt,
                                     const Config &config);

/**
 * @brief Append a new trace history point while respecting trace history limits.
 * @param track Track state whose trace is updated.
 * @param point New trace sample point.
 * @param config Tracker configuration defining history constraints.
 * @return None.
 */
void appendTracePoint(TrackState &track,
                      const Point2f &point,
                      float kalmanDt,
                      const Config &config);

} // namespace opk::tracker::trackstate
