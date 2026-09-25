/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "TrackState.h"

#include "Log.h"
#include "Tracker.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace opk::tracker::trackstate {

namespace {

Point2f makePoint(float x, float y) {
    return Point2f{x, y};
}

} // namespace

void clearPredictionFlag(TrackState &track) {
    track.predictedThisFrame = false;
}

Point2f predictCenter(TrackState &track, float kalmanDt, const Config &config) {
    using StateVector = TrackState::Kalman::StateVector;
    using StateMatrix = TrackState::Kalman::StateMatrix;

    if (!track.kalmanInitialized) {
        assert(track.lastDetection.box);
        const auto &box = *track.lastDetection.box;
        const float centerX = box.x + (box.width * 0.5f);
        const float centerY = box.y + (box.height * 0.5f);

        StateVector initialState{};
        initialState[0][0] = centerX;
        initialState[1][0] = centerY;
        initialState[2][0] = 0.0f;
        initialState[3][0] = 0.0f;

        StateMatrix initialCovariance{};
        initialCovariance[0][0] = config.kalmanInitialCovariancePos;
        initialCovariance[1][1] = config.kalmanInitialCovariancePos;
        initialCovariance[2][2] = config.kalmanInitialCovarianceVel;
        initialCovariance[3][3] = config.kalmanInitialCovarianceVel;

        track.kalman.setState(initialState);
        track.kalman.setCovariance(initialCovariance);
        track.kalmanInitialized = true;
        return makePoint(centerX, centerY);
    }

    StateMatrix transition{};
    transition[0][0] = 1.0f;
    transition[0][2] = kalmanDt;
    transition[1][1] = 1.0f;
    transition[1][3] = kalmanDt;
    transition[2][2] = 1.0f;
    transition[3][3] = 1.0f;

    StateMatrix processNoise{};
    processNoise[0][0] = config.kalmanProcessNoisePos;
    processNoise[1][1] = config.kalmanProcessNoisePos;
    processNoise[2][2] = config.kalmanProcessNoiseVel;
    processNoise[3][3] = config.kalmanProcessNoiseVel;

    track.kalman.predict(transition, processNoise);

    const auto &state = track.kalman.state();
    return makePoint(state[0][0], state[1][0]);
}

Point2f correctCenterWithMeasurement(TrackState &track,
                                     const open_perception_kit::metadata::BoxDetectionT &detection,
                                     float kalmanDt,
                                     const Config &config) {
    using MeasurementVector = TrackState::Kalman::MeasurementVector;
    using MeasurementMatrix = TrackState::Kalman::MeasurementMatrix;
    using ObservationMatrix = TrackState::Kalman::ObservationMatrix;

    if (!track.predictedThisFrame) {
        predictCenter(track, kalmanDt, config);
        track.predictedThisFrame = true;
    }

    assert(detection.box);
    const auto &box = *detection.box;
    const float measX = box.x + (box.width * 0.5f);
    const float measY = box.y + (box.height * 0.5f);

    MeasurementVector measurement{};
    measurement[0][0] = measX;
    measurement[1][0] = measY;

    ObservationMatrix observation{};
    observation[0][0] = 1.0f;
    observation[1][1] = 1.0f;

    MeasurementMatrix measurementNoise{};
    measurementNoise[0][0] = config.kalmanMeasurementNoisePos;
    measurementNoise[1][1] = config.kalmanMeasurementNoisePos;

    if (!track.kalman.update(measurement, observation, measurementNoise)) {
        opk::log::warning(
            "[opktracker] Skipping Kalman correction because innovation covariance is singular\n");
        return makePoint(measX, measY);
    }

    const auto &state = track.kalman.state();
    return makePoint(state[0][0], state[1][0]);
}

void appendTracePoint(TrackState &track,
                      const Point2f &point,
                      float kalmanDt,
                      const Config &config) {
    auto &tracePoint = track.traceHistoryPoints.emplace_back();
    tracePoint.x = point.x;
    tracePoint.y = point.y;

    int historyPoints = 1;
    if (config.traceHistorySeconds > 0.0f && kalmanDt > 0.0f) {
        historyPoints = static_cast<int>(std::ceil(config.traceHistorySeconds / kalmanDt));
    }

    const auto maxHistorySize = static_cast<size_t>(std::max(1, historyPoints));
    while (track.traceHistoryPoints.size() > maxHistorySize) {
        track.traceHistoryPoints.pop_front();
    }
}

} // namespace opk::tracker::trackstate
