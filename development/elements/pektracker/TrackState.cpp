/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "TrackState.h"

#include "Tracker.h"

#include <algorithm>
#include <cmath>

namespace pek::tracker::trackstate {

void clearPredictionFlag(TrackState &track) {
    track.predictedThisFrame = false;
}

pek::Perception::TrackTrace::Point predictCenter(TrackState &track, const Config &config) {
    using StateVector = TrackState::Kalman::StateVector;
    using StateMatrix = TrackState::Kalman::StateMatrix;

    if (!track.kalmanInitialized) {
        const float centerX = track.lastDetection.x + (track.lastDetection.width * 0.5f);
        const float centerY = track.lastDetection.y + (track.lastDetection.height * 0.5f);

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
        return {centerX, centerY};
    }

    StateMatrix transition{};
    transition[0][0] = 1.0f;
    transition[0][2] = config.kalmanDt;
    transition[1][1] = 1.0f;
    transition[1][3] = config.kalmanDt;
    transition[2][2] = 1.0f;
    transition[3][3] = 1.0f;

    StateMatrix processNoise{};
    processNoise[0][0] = config.kalmanProcessNoisePos;
    processNoise[1][1] = config.kalmanProcessNoisePos;
    processNoise[2][2] = config.kalmanProcessNoiseVel;
    processNoise[3][3] = config.kalmanProcessNoiseVel;

    track.kalman.predict(transition, processNoise);

    const auto &state = track.kalman.state();
    return {state[0][0], state[1][0]};
}

pek::Perception::TrackTrace::Point correctCenterWithMeasurement(
    TrackState &track, const pek::Perception::Rect &detection, const Config &config) {
    using MeasurementVector = TrackState::Kalman::MeasurementVector;
    using MeasurementMatrix = TrackState::Kalman::MeasurementMatrix;
    using ObservationMatrix = TrackState::Kalman::ObservationMatrix;

    if (!track.predictedThisFrame) {
        predictCenter(track, config);
        track.predictedThisFrame = true;
    }

    const float measX = detection.x + (detection.width * 0.5f);
    const float measY = detection.y + (detection.height * 0.5f);

    MeasurementVector measurement{};
    measurement[0][0] = measX;
    measurement[1][0] = measY;

    ObservationMatrix observation{};
    observation[0][0] = 1.0f;
    observation[1][1] = 1.0f;

    MeasurementMatrix measurementNoise{};
    measurementNoise[0][0] = config.kalmanMeasurementNoisePos;
    measurementNoise[1][1] = config.kalmanMeasurementNoisePos;

    track.kalman.update(measurement, observation, measurementNoise);

    const auto &state = track.kalman.state();
    return {state[0][0], state[1][0]};
}

void appendTracePoint(TrackState &track,
                      const pek::Perception::TrackTrace::Point &point,
                      const Config &config) {
    track.traceHistoryPoints.push_back(point);

    int historyPoints = 1;
    if (config.traceHistorySeconds > 0.0f && config.kalmanDt > 0.0f) {
        historyPoints = static_cast<int>(std::ceil(config.traceHistorySeconds / config.kalmanDt));
    }

    const auto maxHistorySize = static_cast<size_t>(std::max(1, historyPoints));
    while (track.traceHistoryPoints.size() > maxHistorySize) {
        track.traceHistoryPoints.pop_front();
    }
}

} // namespace pek::tracker::trackstate
