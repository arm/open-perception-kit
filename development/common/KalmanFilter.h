/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "Matrix.h"

#include <cstdint>

template <uint32_t STATE_DIM, uint32_t MEAS_DIM, typename T = double> class KalmanFilter {
  public:
    using StateVector = Matrix<STATE_DIM, 1, T>;
    using StateMatrix = Matrix<STATE_DIM, STATE_DIM, T>;
    using MeasurementVector = Matrix<MEAS_DIM, 1, T>;
    using MeasurementMatrix = Matrix<MEAS_DIM, MEAS_DIM, T>;
    using ObservationMatrix = Matrix<MEAS_DIM, STATE_DIM, T>;
    using KalmanGainMatrix = Matrix<STATE_DIM, MEAS_DIM, T>;

    KalmanFilter()
        : stateValue{}, covarianceValue{StateMatrix::template identity<STATE_DIM, STATE_DIM>()} {}

    KalmanFilter(const StateVector &initialState, const StateMatrix &initialCovariance)
        : stateValue(initialState), covarianceValue(initialCovariance) {}

    const StateVector &state() const {
        return stateValue;
    }

    const StateMatrix &covariance() const {
        return covarianceValue;
    }

    void setState(const StateVector &value) {
        stateValue = value;
    }

    void setCovariance(const StateMatrix &value) {
        covarianceValue = value;
    }

    void predict(const StateMatrix &transitionMatrix, const StateMatrix &processNoise) {
        stateValue = transitionMatrix * stateValue;
        covarianceValue =
            transitionMatrix * covarianceValue * transitionMatrix.transpose() + processNoise;
    }

    template <uint32_t CONTROL_DIM>
    void predict(const StateMatrix &transitionMatrix,
                 const StateMatrix &processNoise,
                 const Matrix<STATE_DIM, CONTROL_DIM, T> &controlMatrix,
                 const Matrix<CONTROL_DIM, 1, T> &controlInput) {
        stateValue = transitionMatrix * stateValue + controlMatrix * controlInput;
        covarianceValue =
            transitionMatrix * covarianceValue * transitionMatrix.transpose() + processNoise;
    }

    void update(const MeasurementVector &measurement,
                const ObservationMatrix &observationMatrix,
                const MeasurementMatrix &measurementNoise) {
        const MeasurementVector innovation = measurement - observationMatrix * stateValue;
        const MeasurementMatrix innovationCovariance =
            observationMatrix * covarianceValue * observationMatrix.transpose() + measurementNoise;

        const KalmanGainMatrix kalmanGain =
            covarianceValue * observationMatrix.transpose() * innovationCovariance.inv();

        stateValue = stateValue + kalmanGain * innovation;

        const StateMatrix identity = StateMatrix::template identity<STATE_DIM, STATE_DIM>();
        covarianceValue = (identity - kalmanGain * observationMatrix) * covarianceValue;
    }

  private:
    StateVector stateValue;
    StateMatrix covarianceValue;
};
