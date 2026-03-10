/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "KalmanFilter.h"
#include "amp/Perception.h"

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace amp::tracker {

struct Config;

struct Identity {
    using Kalman = KalmanFilter<4, 2, float>;
    uint64_t identityId = 0;
    amp::Perception::Rect lastDetection;
    std::string lastMatchDiagnostic = "NEW";
    std::deque<amp::Perception::TrackTrace::Point> tracePoints;
    bool kalmanInitialized = false;
    bool predictedThisFrame = false;
    Kalman kalman;
    std::vector<float> lastEmbedding;
    bool hasEmbedding = false;
    int missedFrames = 0;
    int hitStreak = 0;
    uint64_t lastUpdateFrame = 0;
};

struct DormantIdentity {
    uint64_t identityId = 0;
    amp::Perception::Rect lastDetection;
    std::vector<float> lastEmbedding;
    uint64_t storedAtFrame = 0;
};

} // namespace amp::tracker

namespace amp::tracker::identity {

/**
 * @brief Reset the per-frame prediction marker for an identity.
 * @param identity Identity to update.
 * @return None.
 */
void clearPredictionFlag(Identity &identity);

/**
 * @brief Run prediction step and return the predicted center point.
 * @param identity Identity whose motion state is predicted.
 * @param config Tracker configuration used by prediction logic.
 * @return Predicted center point of the identity.
 */
Perception::TrackTrace::Point predictCenter(Identity &identity, const Config &config);

/**
 * @brief Correct identity state using the latest detection measurement.
 * @param identity Identity whose state is corrected.
 * @param detection Detection rectangle used as measurement.
 * @param config Tracker configuration used by correction logic.
 * @return Corrected/smoothed center point after measurement update.
 */
Perception::TrackTrace::Point correctCenterWithMeasurement(Identity &identity,
                                                           const Perception::Rect &detection,
                                                           const Config &config);

/**
 * @brief Append a new trace point while respecting trace history limits.
 * @param identity Identity whose trace is updated.
 * @param point New trace sample point.
 * @param config Tracker configuration defining history constraints.
 * @return None.
 */
void appendTraceSample(Identity &identity,
                       const Perception::TrackTrace::Point &point,
                       const Config &config);

} // namespace amp::tracker::identity
