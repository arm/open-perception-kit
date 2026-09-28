/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

#include <gtest/gtest.h>

#include "opk/KalmanFilter.h"

namespace {

using Cv2DKf = opk::KalmanFilter<4, 2, double>;
using StateVector = Cv2DKf::StateVector;
using StateMatrix = Cv2DKf::StateMatrix;
using MeasurementVector = Cv2DKf::MeasurementVector;
using MeasurementMatrix = Cv2DKf::MeasurementMatrix;
using ObservationMatrix = Cv2DKf::ObservationMatrix;

StateMatrix makeTransition(double dt) {
    StateMatrix transition{};
    transition[0][0] = 1.0;
    transition[0][2] = dt;
    transition[1][1] = 1.0;
    transition[1][3] = dt;
    transition[2][2] = 1.0;
    transition[3][3] = 1.0;
    return transition;
}

StateMatrix makeProcessNoise(double posNoise, double velNoise) {
    StateMatrix processNoise{};
    processNoise[0][0] = posNoise;
    processNoise[1][1] = posNoise;
    processNoise[2][2] = velNoise;
    processNoise[3][3] = velNoise;
    return processNoise;
}

ObservationMatrix makeObservation() {
    ObservationMatrix observation{};
    observation[0][0] = 1.0;
    observation[1][1] = 1.0;
    return observation;
}

MeasurementMatrix makeMeasurementNoise(double posNoise) {
    MeasurementMatrix measurementNoise{};
    measurementNoise[0][0] = posNoise;
    measurementNoise[1][1] = posNoise;
    return measurementNoise;
}

} // namespace

TEST(KalmanFilterCv2D, PredictAdvancesPositionWithConstantVelocity) {
    StateVector initialState{};
    initialState[0][0] = 0.0;
    initialState[1][0] = 0.0;
    initialState[2][0] = 1.0;
    initialState[3][0] = 2.0;

    StateMatrix initialCovariance = StateMatrix::identity();

    Cv2DKf kf(initialState, initialCovariance);
    kf.predict(makeTransition(1.0), makeProcessNoise(0.01, 0.01));

    const auto &state = kf.state();
    EXPECT_NEAR(state[0][0], 1.0, 1e-9);
    EXPECT_NEAR(state[1][0], 2.0, 1e-9);
    EXPECT_NEAR(state[2][0], 1.0, 1e-9);
    EXPECT_NEAR(state[3][0], 2.0, 1e-9);

    const auto &covariance = kf.covariance();
    EXPECT_NEAR(covariance[0][0], 2.01, 1e-9);
    EXPECT_NEAR(covariance[1][1], 2.01, 1e-9);
    EXPECT_NEAR(covariance[2][2], 1.01, 1e-9);
    EXPECT_NEAR(covariance[3][3], 1.01, 1e-9);
}

TEST(KalmanFilterCv2D, UpdateMovesEstimateTowardMeasurementAndReducesPositionCovariance) {
    StateVector initialState{};
    initialState[0][0] = 0.0;
    initialState[1][0] = 0.0;
    initialState[2][0] = 1.0;
    initialState[3][0] = 2.0;

    Cv2DKf kf(initialState, StateMatrix::identity());
    kf.predict(makeTransition(1.0), makeProcessNoise(0.01, 0.01));

    const auto priorState = kf.state();
    const auto priorCovariance = kf.covariance();

    MeasurementVector measurement{};
    measurement[0][0] = 1.2;
    measurement[1][0] = 1.8;

    ASSERT_TRUE(kf.update(measurement, makeObservation(), makeMeasurementNoise(0.25)));

    const auto &posteriorState = kf.state();
    EXPECT_LT(std::abs(posteriorState[0][0] - measurement[0][0]),
              std::abs(priorState[0][0] - measurement[0][0]));
    EXPECT_LT(std::abs(posteriorState[1][0] - measurement[1][0]),
              std::abs(priorState[1][0] - measurement[1][0]));

    const auto &posteriorCovariance = kf.covariance();
    EXPECT_LT(posteriorCovariance[0][0], priorCovariance[0][0]);
    EXPECT_LT(posteriorCovariance[1][1], priorCovariance[1][1]);
}

TEST(KalmanFilterCv2D, RepeatedPredictUpdateConvergesToConstantVelocityTrajectory) {
    StateVector initialState{};
    initialState[0][0] = 0.0;
    initialState[1][0] = 0.0;
    initialState[2][0] = 0.0;
    initialState[3][0] = 0.0;

    StateMatrix initialCovariance{};
    initialCovariance[0][0] = 50.0;
    initialCovariance[1][1] = 50.0;
    initialCovariance[2][2] = 50.0;
    initialCovariance[3][3] = 50.0;

    Cv2DKf kf(initialState, initialCovariance);

    const auto transition = makeTransition(1.0);
    const auto processNoise = makeProcessNoise(0.01, 0.01);
    const auto observation = makeObservation();
    const auto measurementNoise = makeMeasurementNoise(0.05);

    for (int t = 1; t <= 6; ++t) {
        kf.predict(transition, processNoise);

        MeasurementVector measurement{};
        measurement[0][0] = static_cast<double>(t);     // x = 1 * t
        measurement[1][0] = static_cast<double>(2 * t); // y = 2 * t
        ASSERT_TRUE(kf.update(measurement, observation, measurementNoise));
    }

    const auto &state = kf.state();
    EXPECT_NEAR(state[0][0], 6.0, 0.15);
    EXPECT_NEAR(state[1][0], 12.0, 0.15);
    EXPECT_NEAR(state[2][0], 1.0, 0.15);
    EXPECT_NEAR(state[3][0], 2.0, 0.15);
}

TEST(KalmanFilterCv2D, RejectsSingularInnovationCovariance) {
    Cv2DKf kf(StateVector{}, StateMatrix{});

    EXPECT_FALSE(kf.update(MeasurementVector{}, makeObservation(), MeasurementMatrix{}));
}
