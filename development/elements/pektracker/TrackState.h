/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/KalmanFilter.h"
#include "pek/Perception.h"

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace pek::tracker {

struct Config;

struct TrackState {
    using Kalman = pek::KalmanFilter<4, 2, float>;
    uint64_t trackId = 0;
    pek::Perception::Rect lastDetection;
    std::string lastMatchDiagnostic = "NEW";
    std::deque<pek::Perception::TrackTrace::Point> traceHistoryPoints;
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
    pek::Perception::Rect lastDetection;
    std::vector<float> lastEmbedding;
    uint64_t storedAtFrame = 0;
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
Perception::TrackTrace::Point predictCenter(TrackState &track, const Config &config);

/**
 * @brief Correct track state using the latest detection measurement.
 * @param track Track state whose state is corrected.
 * @param detection Detection rectangle used as measurement.
 * @param config Tracker configuration used by correction logic.
 * @return Corrected/smoothed center point after measurement update.
 */
Perception::TrackTrace::Point correctCenterWithMeasurement(TrackState &track,
                                                           const Perception::Rect &detection,
                                                           const Config &config);

/**
 * @brief Append a new trace history point while respecting trace history limits.
 * @param track Track state whose trace is updated.
 * @param point New trace sample point.
 * @param config Tracker configuration defining history constraints.
 * @return None.
 */
void appendTracePoint(TrackState &track,
                      const Perception::TrackTrace::Point &point,
                      const Config &config);

} // namespace pek::tracker::trackstate
