/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Matrix.h"

#include <cstdint>

namespace pek {

/**
 * @brief Generic discrete-time Kalman filter with fixed dimensions.
 *
 * Implements predict and update steps for linear systems using compile-time
 * matrix dimensions.
 *
 * @tparam STATE_DIM State vector dimension.
 * @tparam MEAS_DIM Measurement vector dimension.
 * @tparam T Scalar type.
 */
template <uint32_t STATE_DIM, uint32_t MEAS_DIM, typename T = double> class KalmanFilter {
  public:
    /// State vector type.
    using StateVector = Matrix<STATE_DIM, 1, T>;
    /// State covariance matrix type.
    using StateMatrix = Matrix<STATE_DIM, STATE_DIM, T>;
    /// Measurement vector type.
    using MeasurementVector = Matrix<MEAS_DIM, 1, T>;
    /// Measurement covariance matrix type.
    using MeasurementMatrix = Matrix<MEAS_DIM, MEAS_DIM, T>;
    /// Observation matrix type.
    using ObservationMatrix = Matrix<MEAS_DIM, STATE_DIM, T>;
    /// Kalman gain matrix type.
    using KalmanGainMatrix = Matrix<STATE_DIM, MEAS_DIM, T>;

    /**
     * @brief Constructs a filter with zero state and identity covariance.
     */
    KalmanFilter()
        : stateValue{}, covarianceValue{StateMatrix::template identity<STATE_DIM, STATE_DIM>()} {}

    /**
     * @brief Constructs a filter with explicit initial state and covariance.
     * @param initialState Initial state vector.
     * @param initialCovariance Initial covariance matrix.
     */
    KalmanFilter(const StateVector &initialState, const StateMatrix &initialCovariance)
        : stateValue(initialState), covarianceValue(initialCovariance) {}

    /**
     * @brief Returns current state estimate.
     * @return Current state vector.
     */
    const StateVector &state() const {
        return stateValue;
    }

    /**
     * @brief Returns current state covariance.
     * @return Current covariance matrix.
     */
    const StateMatrix &covariance() const {
        return covarianceValue;
    }

    /**
     * @brief Sets current state estimate.
     * @param value New state vector.
     */
    void setState(const StateVector &value) {
        stateValue = value;
    }

    /**
     * @brief Sets current state covariance.
     * @param value New covariance matrix.
     */
    void setCovariance(const StateMatrix &value) {
        covarianceValue = value;
    }

    /**
     * @brief Predict step without control input.
     * @param transitionMatrix State transition matrix.
     * @param processNoise Process noise covariance.
     */
    void predict(const StateMatrix &transitionMatrix, const StateMatrix &processNoise) {
        stateValue = transitionMatrix * stateValue;
        covarianceValue =
            transitionMatrix * covarianceValue * transitionMatrix.transpose() + processNoise;
    }

    /**
     * @brief Predict step with control input.
     * @tparam CONTROL_DIM Control vector dimension.
     * @param transitionMatrix State transition matrix.
     * @param processNoise Process noise covariance.
     * @param controlMatrix Control model matrix.
     * @param controlInput Control vector.
     */
    template <uint32_t CONTROL_DIM>
    void predict(const StateMatrix &transitionMatrix,
                 const StateMatrix &processNoise,
                 const Matrix<STATE_DIM, CONTROL_DIM, T> &controlMatrix,
                 const Matrix<CONTROL_DIM, 1, T> &controlInput) {
        stateValue = transitionMatrix * stateValue + controlMatrix * controlInput;
        covarianceValue =
            transitionMatrix * covarianceValue * transitionMatrix.transpose() + processNoise;
    }

    /**
     * @brief Update step with a measurement.
     * @param measurement Measurement vector.
     * @param observationMatrix Observation matrix.
     * @param measurementNoise Measurement noise covariance.
     */
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

} // namespace pek
