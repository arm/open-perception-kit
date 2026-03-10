/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "Identity.h"

#include "Tracker.h"

#include <algorithm>
#include <cmath>

namespace amp::tracker::identity {

void clearPredictionFlag(Identity &identity) {
    identity.predictedThisFrame = false;
}

amp::Perception::TrackTrace::Point predictCenter(Identity &identity, const Config &config) {
    using StateVector = Identity::Kalman::StateVector;
    using StateMatrix = Identity::Kalman::StateMatrix;

    if (!identity.kalmanInitialized) {
        const float centerX = identity.lastDetection.x + (identity.lastDetection.width * 0.5f);
        const float centerY = identity.lastDetection.y + (identity.lastDetection.height * 0.5f);

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

        identity.kalman.setState(initialState);
        identity.kalman.setCovariance(initialCovariance);
        identity.kalmanInitialized = true;
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

    identity.kalman.predict(transition, processNoise);

    const auto &state = identity.kalman.state();
    return {state[0][0], state[1][0]};
}

amp::Perception::TrackTrace::Point correctCenterWithMeasurement(
    Identity &identity, const amp::Perception::Rect &detection, const Config &config) {
    using MeasurementVector = Identity::Kalman::MeasurementVector;
    using MeasurementMatrix = Identity::Kalman::MeasurementMatrix;
    using ObservationMatrix = Identity::Kalman::ObservationMatrix;

    if (!identity.predictedThisFrame) {
        predictCenter(identity, config);
        identity.predictedThisFrame = true;
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

    identity.kalman.update(measurement, observation, measurementNoise);

    const auto &state = identity.kalman.state();
    return {state[0][0], state[1][0]};
}

void appendTraceSample(Identity &identity,
                       const amp::Perception::TrackTrace::Point &point,
                       const Config &config) {
    identity.tracePoints.push_back(point);

    int historyPoints = 1;
    if (config.traceHistorySeconds > 0.0f && config.kalmanDt > 0.0f) {
        historyPoints = static_cast<int>(std::ceil(config.traceHistorySeconds / config.kalmanDt));
    }

    const auto maxHistorySize = static_cast<size_t>(std::max(1, historyPoints));
    while (identity.tracePoints.size() > maxHistorySize) {
        identity.tracePoints.pop_front();
    }
}

} // namespace amp::tracker::identity
