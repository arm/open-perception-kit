/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <type_traits>

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

    Matrix() {
        data.fill(static_cast<T>(0));
    }

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

    RowProxy<T> operator[](size_t r) {
        assert(r < ROWS);
        return RowProxy<T>(&data[r * COLS]);
    }

    RowProxy<const T> operator[](size_t r) const {
        assert(r < ROWS);
        return RowProxy<const T>(&data[r * COLS]);
    }

    Matrix operator+(const Matrix &rhs) const {
        Matrix out{};
        for (size_t i = 0; i < data.size(); ++i) {
            out.data[i] = data[i] + rhs.data[i];
        }
        return out;
    }

    Matrix operator-(const Matrix &rhs) const {
        Matrix out{};
        for (size_t i = 0; i < data.size(); ++i) {
            out.data[i] = data[i] - rhs.data[i];
        }
        return out;
    }

    Matrix &operator+=(const Matrix &rhs) {
        for (size_t i = 0; i < data.size(); ++i) {
            data[i] += rhs.data[i];
        }
        return *this;
    }

    Matrix &operator-=(const Matrix &rhs) {
        for (size_t i = 0; i < data.size(); ++i) {
            data[i] -= rhs.data[i];
        }
        return *this;
    }

    Matrix operator*(T scalar) const {
        Matrix out{};
        for (size_t i = 0; i < data.size(); ++i) {
            out.data[i] = data[i] * scalar;
        }
        return out;
    }

    Matrix operator/(T scalar) const {
        assert(scalar != static_cast<T>(0));
        Matrix out{};
        for (size_t i = 0; i < data.size(); ++i) {
            out.data[i] = data[i] / scalar;
        }
        return out;
    }

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

    Matrix<COLS, ROWS, T> transpose() const {
        Matrix<COLS, ROWS, T> out{};
        for (size_t r = 0; r < ROWS; ++r) {
            for (size_t c = 0; c < COLS; ++c) {
                out[c][r] = (*this)[r][c];
            }
        }
        return out;
    }

    template <uint32_t R = ROWS, uint32_t C = COLS>
    static typename std::enable_if<R == C, Matrix<R, C, T>>::type identity() {
        Matrix<R, C, T> out{};
        for (size_t i = 0; i < R; ++i) {
            out[i][i] = static_cast<T>(1);
        }
        return out;
    }

    template <uint32_t R = ROWS, uint32_t C = COLS>
    typename std::enable_if<(R == C), Matrix<R, C, T>>::type inv() const {
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

            assert(bestAbs != static_cast<T>(0));

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
            assert(pivotValue != static_cast<T>(0));

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

    static constexpr uint32_t rows() {
        return ROWS;
    }

    static constexpr uint32_t cols() {
        return COLS;
    }

  private:
    std::array<T, ROWS * COLS> data{};
};

template <uint32_t ROWS, uint32_t COLS, typename T>
Matrix<ROWS, COLS, T> operator*(T scalar, const Matrix<ROWS, COLS, T> &rhs) {
    return rhs * scalar;
}
