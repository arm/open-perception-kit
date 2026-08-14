/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/FrameResults.h"
#include "pek/KalmanFilter.h"

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pek::tracker {

struct Config;

struct Point2f {
    float x = 0.0f;
    float y = 0.0f;
};

struct TrackState {
    using Kalman = pek::KalmanFilter<4, 2, float>;
    uint64_t trackId = 0;
    perception::metadata::BoxDetectionT lastDetection;
    std::string lastMatchDiagnostic = "NEW";
    std::deque<perception::metadata::Point2fT> traceHistoryPoints;
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
    perception::metadata::BoxDetectionT lastDetection;
    std::vector<float> lastEmbedding;
    // Fallback expiration baseline used only when comparable running times are unavailable.
    uint64_t storedAtFrame = 0;
    // Preferred expiration baseline derived from the buffer PTS and current GstSegment.
    std::optional<uint64_t> storedAtRunningTimeMs;
};

} // namespace pek::tracker

namespace pek::tracker::trackstate {

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
                                     const perception::metadata::BoxDetectionT &detection,
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

} // namespace pek::tracker::trackstate
