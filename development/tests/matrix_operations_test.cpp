/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include "Matrix.h"

namespace {

template <uint32_t R, uint32_t C>
void ExpectMatrixNear(const Matrix<R, C, double> &actual,
                      const Matrix<R, C, double> &expected,
                      double epsilon = 1e-6) {
    for (size_t r = 0; r < R; ++r) {
        for (size_t c = 0; c < C; ++c) {
            EXPECT_NEAR(actual[r][c], expected[r][c], epsilon) << "at (" << r << "," << c << ")";
        }
    }
}

template <uint32_t N>
void ExpectIdentityNear(const Matrix<N, N, double> &m, double epsilon = 1e-6) {
    const auto identity = Matrix<N, N, double>::identity();
    ExpectMatrixNear(m, identity, epsilon);
}

} // namespace

TEST(MatrixOps, InitRowsAndColsFiveByFive) {
    EXPECT_EQ((Matrix<5, 5, double>::rows()), 5U);
    EXPECT_EQ((Matrix<5, 5, double>::cols()), 5U);
}

TEST(MatrixOps, IndexingReadWriteFiveByFive) {
    Matrix<5, 5, double> m{{
        {1.0, 2.0, 3.0, 4.0, 5.0},
        {6.0, 7.0, 8.0, 9.0, 10.0},
        {11.0, 12.0, 13.0, 14.0, 15.0},
        {16.0, 17.0, 18.0, 19.0, 20.0},
        {21.0, 22.0, 23.0, 24.0, 25.0},
    }};

    EXPECT_DOUBLE_EQ(m[0][0], 1.0);
    EXPECT_DOUBLE_EQ(m[4][4], 25.0);

    m[2][3] = 140.0;
    EXPECT_DOUBLE_EQ(m[2][3], 140.0);
}

TEST(MatrixOps, AddFiveByFive) {
    Matrix<5, 5, double> a{{
        {1.0, 2.0, 3.0, 4.0, 5.0},
        {6.0, 7.0, 8.0, 9.0, 10.0},
        {11.0, 12.0, 13.0, 14.0, 15.0},
        {16.0, 17.0, 18.0, 19.0, 20.0},
        {21.0, 22.0, 23.0, 24.0, 25.0},
    }};

    Matrix<5, 5, double> b{{
        {25.0, 24.0, 23.0, 22.0, 21.0},
        {20.0, 19.0, 18.0, 17.0, 16.0},
        {15.0, 14.0, 13.0, 12.0, 11.0},
        {10.0, 9.0, 8.0, 7.0, 6.0},
        {5.0, 4.0, 3.0, 2.0, 1.0},
    }};

    const auto sum = a + b;

    Matrix<5, 5, double> expectedSum{{
        {26.0, 26.0, 26.0, 26.0, 26.0},
        {26.0, 26.0, 26.0, 26.0, 26.0},
        {26.0, 26.0, 26.0, 26.0, 26.0},
        {26.0, 26.0, 26.0, 26.0, 26.0},
        {26.0, 26.0, 26.0, 26.0, 26.0},
    }};

    ExpectMatrixNear(sum, expectedSum);
}

TEST(MatrixOps, SubtractFiveByFive) {
    Matrix<5, 5, double> a{{
        {1.0, 2.0, 3.0, 4.0, 5.0},
        {6.0, 7.0, 8.0, 9.0, 10.0},
        {11.0, 12.0, 13.0, 14.0, 15.0},
        {16.0, 17.0, 18.0, 19.0, 20.0},
        {21.0, 22.0, 23.0, 24.0, 25.0},
    }};

    Matrix<5, 5, double> b{{
        {25.0, 24.0, 23.0, 22.0, 21.0},
        {20.0, 19.0, 18.0, 17.0, 16.0},
        {15.0, 14.0, 13.0, 12.0, 11.0},
        {10.0, 9.0, 8.0, 7.0, 6.0},
        {5.0, 4.0, 3.0, 2.0, 1.0},
    }};

    const auto diff = a - b;

    Matrix<5, 5, double> expectedDiff{{
        {-24.0, -22.0, -20.0, -18.0, -16.0},
        {-14.0, -12.0, -10.0, -8.0, -6.0},
        {-4.0, -2.0, 0.0, 2.0, 4.0},
        {6.0, 8.0, 10.0, 12.0, 14.0},
        {16.0, 18.0, 20.0, 22.0, 24.0},
    }};

    ExpectMatrixNear(diff, expectedDiff);
}

TEST(MatrixOps, AddAssignFiveByFive) {
    Matrix<5, 5, double> a{{
        {1.0, 2.0, 3.0, 4.0, 5.0},
        {6.0, 7.0, 8.0, 9.0, 10.0},
        {11.0, 12.0, 13.0, 14.0, 15.0},
        {16.0, 17.0, 18.0, 19.0, 20.0},
        {21.0, 22.0, 23.0, 24.0, 25.0},
    }};

    Matrix<5, 5, double> b{{
        {25.0, 24.0, 23.0, 22.0, 21.0},
        {20.0, 19.0, 18.0, 17.0, 16.0},
        {15.0, 14.0, 13.0, 12.0, 11.0},
        {10.0, 9.0, 8.0, 7.0, 6.0},
        {5.0, 4.0, 3.0, 2.0, 1.0},
    }};

    Matrix<5, 5, double> expectedSum{{
        {26.0, 26.0, 26.0, 26.0, 26.0},
        {26.0, 26.0, 26.0, 26.0, 26.0},
        {26.0, 26.0, 26.0, 26.0, 26.0},
        {26.0, 26.0, 26.0, 26.0, 26.0},
        {26.0, 26.0, 26.0, 26.0, 26.0},
    }};

    a += b;
    ExpectMatrixNear(a, expectedSum);
}

TEST(MatrixOps, SubtractAssignFiveByFive) {
    Matrix<5, 5, double> a{{
        {26.0, 26.0, 26.0, 26.0, 26.0},
        {26.0, 26.0, 26.0, 26.0, 26.0},
        {26.0, 26.0, 26.0, 26.0, 26.0},
        {26.0, 26.0, 26.0, 26.0, 26.0},
        {26.0, 26.0, 26.0, 26.0, 26.0},
    }};

    Matrix<5, 5, double> b{{
        {25.0, 24.0, 23.0, 22.0, 21.0},
        {20.0, 19.0, 18.0, 17.0, 16.0},
        {15.0, 14.0, 13.0, 12.0, 11.0},
        {10.0, 9.0, 8.0, 7.0, 6.0},
        {5.0, 4.0, 3.0, 2.0, 1.0},
    }};

    a -= b;
    Matrix<5, 5, double> originalA{{
        {1.0, 2.0, 3.0, 4.0, 5.0},
        {6.0, 7.0, 8.0, 9.0, 10.0},
        {11.0, 12.0, 13.0, 14.0, 15.0},
        {16.0, 17.0, 18.0, 19.0, 20.0},
        {21.0, 22.0, 23.0, 24.0, 25.0},
    }};
    ExpectMatrixNear(a, originalA);
}

TEST(MatrixOps, ScalarMultiplyRightFiveByFive) {
    Matrix<5, 5, double> a{{
        {1.0, 2.0, 3.0, 4.0, 5.0},
        {6.0, 7.0, 8.0, 9.0, 10.0},
        {11.0, 12.0, 13.0, 14.0, 15.0},
        {16.0, 17.0, 18.0, 19.0, 20.0},
        {21.0, 22.0, 23.0, 24.0, 25.0},
    }};

    const auto twoA = a * 2.0;
    ExpectMatrixNear(twoA / 2.0, a);
}

TEST(MatrixOps, ScalarMultiplyLeftFiveByFive) {
    Matrix<5, 5, double> a{{
        {1.0, 2.0, 3.0, 4.0, 5.0},
        {6.0, 7.0, 8.0, 9.0, 10.0},
        {11.0, 12.0, 13.0, 14.0, 15.0},
        {16.0, 17.0, 18.0, 19.0, 20.0},
        {21.0, 22.0, 23.0, 24.0, 25.0},
    }};

    const auto threeA = 3.0 * a;
    ExpectMatrixNear(threeA / 3.0, a);
}

TEST(MatrixOps, ScalarDivideFiveByFive) {
    Matrix<5, 5, double> a{{
        {1.0, 2.0, 3.0, 4.0, 5.0},
        {6.0, 7.0, 8.0, 9.0, 10.0},
        {11.0, 12.0, 13.0, 14.0, 15.0},
        {16.0, 17.0, 18.0, 19.0, 20.0},
        {21.0, 22.0, 23.0, 24.0, 25.0},
    }};

    const auto halfA = a / 2.0;
    ExpectMatrixNear(halfA * 2.0, a);
}

TEST(MatrixOps, MatmulIdentityFiveByFive) {
    Matrix<5, 5, double> a{{
        {1, 2, 3, 4, 5},
        {0, 1, 0, 1, 0},
        {2, 0, 2, 0, 2},
        {1, 1, 1, 1, 1},
        {5, 4, 3, 2, 1},
    }};

    const auto i = Matrix<5, 5, double>::identity();
    ExpectMatrixNear(a * i, a);
    ExpectMatrixNear(i * a, a);
}

TEST(MatrixOps, TransposeFiveByFive) {
    Matrix<5, 5, double> a{{
        {1, 2, 3, 4, 5},
        {0, 1, 0, 1, 0},
        {2, 0, 2, 0, 2},
        {1, 1, 1, 1, 1},
        {5, 4, 3, 2, 1},
    }};

    const auto t = a.transpose();
    EXPECT_DOUBLE_EQ(t[0][4], a[4][0]);
    EXPECT_DOUBLE_EQ(t[3][1], a[1][3]);
}

TEST(MatrixOps, DoubleTransposeFiveByFive) {
    Matrix<5, 5, double> a{{
        {1, 2, 3, 4, 5},
        {0, 1, 0, 1, 0},
        {2, 0, 2, 0, 2},
        {1, 1, 1, 1, 1},
        {5, 4, 3, 2, 1},
    }};

    const auto t = a.transpose();
    const auto tt = t.transpose();
    ExpectMatrixNear(tt, a);
}

TEST(MatrixOps, InverseFiveByFiveLeftIdentity) {
    Matrix<5, 5, double> b{{
        {1.0, 2.0, 0.0, 1.0, 3.0},
        {0.0, 1.0, 2.0, 0.0, 1.0},
        {3.0, 0.0, 1.0, 2.0, 0.0},
        {2.0, 1.0, 0.0, 1.0, 2.0},
        {1.0, 0.0, 3.0, 1.0, 1.0},
    }};

    const auto a = b.transpose() * b + 0.5 * Matrix<5, 5, double>::identity();
    const auto inv = a.inv();

    ExpectIdentityNear(a * inv, 1e-5);
}

TEST(MatrixOps, InverseFiveByFiveRightIdentity) {
    Matrix<5, 5, double> b{{
        {1.0, 2.0, 0.0, 1.0, 3.0},
        {0.0, 1.0, 2.0, 0.0, 1.0},
        {3.0, 0.0, 1.0, 2.0, 0.0},
        {2.0, 1.0, 0.0, 1.0, 2.0},
        {1.0, 0.0, 3.0, 1.0, 1.0},
    }};

    const auto a = b.transpose() * b + 0.5 * Matrix<5, 5, double>::identity();
    const auto inv = a.inv();

    ExpectIdentityNear(inv * a, 1e-5);
}
