/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <type_traits>

namespace opk {

/**
 * @brief Fixed-size matrix with compile-time dimensions.
 *
 * Provides basic linear algebra operations for arithmetic element types,
 * including addition, subtraction, scalar and matrix multiplication,
 * transpose, identity, and inversion for square matrices.
 *
 * @tparam ROWS Matrix row count.
 * @tparam COLS Matrix column count.
 * @tparam T Element type.
 */
template <uint32_t ROWS, uint32_t COLS, typename T = double> class Matrix {
  private:
    template <typename Item> class RowProxy {
      public:
        explicit RowProxy(Item *row) : row(row) {}

        Item &operator[](size_t c) {
            assert(c < COLS);
            return row[c];
        }

        Item operator[](size_t c) const {
            assert(c < COLS);
            return row[c];
        }

      private:
        Item *row;
    };

  public:
    static_assert(std::is_arithmetic<T>::value, "Matrix requires arithmetic type");

    /**
     * @brief Constructs a zero-initialized matrix.
     */
    Matrix() {
        data.fill(static_cast<T>(0));
    }

    /**
     * @brief Constructs a matrix from nested initializer lists.
     * @param values Row-major matrix values.
     */
    Matrix(std::initializer_list<std::initializer_list<T>> values) : Matrix() {
        assert(values.size() == ROWS);
        size_t r = 0;
        for (const auto &rowValues : values) {
            assert(rowValues.size() == COLS);
            size_t c = 0;
            for (const auto &value : rowValues) {
                (*this)[r][c++] = value;
            }
            ++r;
        }
    }

    /**
     * @brief Returns mutable row access proxy.
     * @param r Row index.
     * @return Row proxy for element access.
     */
    RowProxy<T> operator[](size_t r) {
        assert(r < ROWS);
        return RowProxy<T>(&data[r * COLS]);
    }

    /**
     * @brief Returns const row access proxy.
     * @param r Row index.
     * @return Const row proxy for element access.
     */
    RowProxy<const T> operator[](size_t r) const {
        assert(r < ROWS);
        return RowProxy<const T>(&data[r * COLS]);
    }

    /**
     * @brief Element-wise matrix addition.
     * @param rhs Right-hand matrix.
     * @return Sum matrix.
     */
    Matrix operator+(const Matrix &rhs) const {
        Matrix out{};
        for (size_t i = 0; i < data.size(); ++i) {
            out.data[i] = data[i] + rhs.data[i];
        }
        return out;
    }

    /**
     * @brief Element-wise matrix subtraction.
     * @param rhs Right-hand matrix.
     * @return Difference matrix.
     */
    Matrix operator-(const Matrix &rhs) const {
        Matrix out{};
        for (size_t i = 0; i < data.size(); ++i) {
            out.data[i] = data[i] - rhs.data[i];
        }
        return out;
    }

    /**
     * @brief In-place element-wise matrix addition.
     * @param rhs Right-hand matrix.
     * @return This matrix.
     */
    Matrix &operator+=(const Matrix &rhs) {
        for (size_t i = 0; i < data.size(); ++i) {
            data[i] += rhs.data[i];
        }
        return *this;
    }

    /**
     * @brief In-place element-wise matrix subtraction.
     * @param rhs Right-hand matrix.
     * @return This matrix.
     */
    Matrix &operator-=(const Matrix &rhs) {
        for (size_t i = 0; i < data.size(); ++i) {
            data[i] -= rhs.data[i];
        }
        return *this;
    }

    /**
     * @brief Scalar multiplication.
     * @param scalar Scalar value.
     * @return Scaled matrix.
     */
    Matrix operator*(T scalar) const {
        Matrix out{};
        for (size_t i = 0; i < data.size(); ++i) {
            out.data[i] = data[i] * scalar;
        }
        return out;
    }

    /**
     * @brief Scalar division.
     * @param scalar Scalar value.
     * @return Scaled matrix.
     */
    Matrix operator/(T scalar) const {
        assert(scalar != static_cast<T>(0));
        Matrix out{};
        for (size_t i = 0; i < data.size(); ++i) {
            out.data[i] = data[i] / scalar;
        }
        return out;
    }

    /**
     * @brief Matrix multiplication.
     * @tparam C2 Right-hand matrix column count.
     * @param rhs Right-hand matrix.
     * @return Product matrix.
     */
    template <uint32_t C2> Matrix<ROWS, C2, T> operator*(const Matrix<COLS, C2, T> &rhs) const {
        Matrix<ROWS, C2, T> out{};
        for (size_t r = 0; r < ROWS; ++r) {
            for (size_t c = 0; c < C2; ++c) {
                T sum{};
                for (size_t k = 0; k < COLS; ++k) {
                    sum += (*this)[r][k] * rhs[k][c];
                }
                out[r][c] = sum;
            }
        }
        return out;
    }

    /**
     * @brief Returns transposed matrix.
     * @return Transposed matrix.
     */
    Matrix<COLS, ROWS, T> transpose() const {
        Matrix<COLS, ROWS, T> out{};
        for (size_t r = 0; r < ROWS; ++r) {
            for (size_t c = 0; c < COLS; ++c) {
                out[c][r] = (*this)[r][c];
            }
        }
        return out;
    }

    /**
     * @brief Creates identity matrix for square dimensions.
     * @tparam R Row count.
     * @tparam C Column count.
     * @return Identity matrix.
     */
    template <uint32_t R = ROWS, uint32_t C = COLS>
    static typename std::enable_if<R == C, Matrix<R, C, T>>::type identity() {
        Matrix<R, C, T> out{};
        for (size_t i = 0; i < R; ++i) {
            out[i][i] = static_cast<T>(1);
        }
        return out;
    }

    /**
     * @brief Computes matrix inverse using Gauss-Jordan elimination.
     * @tparam R Row count.
     * @tparam C Column count.
     * @return Inverse matrix, or no value when the matrix is singular.
     */
    template <uint32_t R = ROWS, uint32_t C = COLS>
    typename std::enable_if<(R == C), std::optional<Matrix<R, C, T>>>::type inv() const {
        Matrix<R, C, T> left = *this;
        Matrix<R, C, T> right = Matrix<R, C, T>::template identity<R, C>();

        for (size_t pivot = 0; pivot < R; ++pivot) {
            size_t bestRow = pivot;
            T bestAbs = std::abs(left[bestRow][pivot]);

            for (size_t row = pivot + 1; row < R; ++row) {
                const T candidate = std::abs(left[row][pivot]);
                if (candidate > bestAbs) {
                    bestAbs = candidate;
                    bestRow = row;
                }
            }

            if (bestAbs == static_cast<T>(0))
                return std::nullopt;

            if (bestRow != pivot) {
                for (size_t c = 0; c < C; ++c) {
                    const T tmpLeft = left[pivot][c];
                    left[pivot][c] = left[bestRow][c];
                    left[bestRow][c] = tmpLeft;

                    const T tmpRight = right[pivot][c];
                    right[pivot][c] = right[bestRow][c];
                    right[bestRow][c] = tmpRight;
                }
            }

            const T pivotValue = left[pivot][pivot];
            if (pivotValue == static_cast<T>(0))
                return std::nullopt;

            for (size_t c = 0; c < C; ++c) {
                left[pivot][c] /= pivotValue;
                right[pivot][c] /= pivotValue;
            }

            for (size_t row = 0; row < R; ++row) {
                if (row == pivot)
                    continue;

                const T factor = left[row][pivot];
                if (factor == static_cast<T>(0))
                    continue;

                for (size_t c = 0; c < C; ++c) {
                    left[row][c] -= factor * left[pivot][c];
                    right[row][c] -= factor * right[pivot][c];
                }
            }
        }

        return right;
    }

    /**
     * @brief Matrix row count.
     * @return Compile-time row count.
     */
    static constexpr uint32_t rows() {
        return ROWS;
    }

    /**
     * @brief Matrix column count.
     * @return Compile-time column count.
     */
    static constexpr uint32_t cols() {
        return COLS;
    }

  private:
    std::array<T, ROWS * COLS> data{};
};

/**
 * @brief Scalar multiplication with scalar on the left side.
 * @tparam ROWS Matrix row count.
 * @tparam COLS Matrix column count.
 * @tparam T Element type.
 * @param scalar Scalar value.
 * @param rhs Right-hand matrix.
 * @return Scaled matrix.
 */
template <uint32_t ROWS, uint32_t COLS, typename T>
Matrix<ROWS, COLS, T> operator*(T scalar, const Matrix<ROWS, COLS, T> &rhs) {
    return rhs * scalar;
}

} // namespace opk
