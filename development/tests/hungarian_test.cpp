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

#include "algo/Hungarian.h"

#include <set>
#include <vector>

namespace {

void ExpectUniqueAssignedColumns(const std::vector<int> &assignment,
                                 size_t expectedAssignedCount,
                                 size_t maxColumnCount) {
    std::set<int> assignedColumns;
    for (const int columnIndex : assignment) {
        if (columnIndex < 0) {
            continue;
        }
        EXPECT_LT(static_cast<size_t>(columnIndex), maxColumnCount);
        assignedColumns.insert(columnIndex);
    }
    EXPECT_EQ(assignedColumns.size(), expectedAssignedCount);
}

} // namespace

TEST(HungarianAlgo, EmptyInputReturnsEmptyAssignment) {
    const std::vector<std::vector<float>> costMatrix;
    const auto assignment = opk::algo::solveHungarian(costMatrix);
    EXPECT_TRUE(assignment.empty());
}

TEST(HungarianAlgo, EmptyColumnsReturnsEmptyAssignment) {
    const std::vector<std::vector<float>> costMatrix = {{}};
    const auto assignment = opk::algo::solveHungarian(costMatrix);
    EXPECT_TRUE(assignment.empty());
}

TEST(HungarianAlgo, SquareMatrixFindsOptimalAssignment) {
    const std::vector<std::vector<int>> costMatrix = {
        {4, 1, 3},
        {2, 0, 5},
        {3, 2, 2},
    };

    const auto assignment = opk::algo::solveHungarian(costMatrix);

    ASSERT_EQ(assignment.size(), 3U);
    EXPECT_EQ(assignment[0], 1);
    EXPECT_EQ(assignment[1], 0);
    EXPECT_EQ(assignment[2], 2);

    ExpectUniqueAssignedColumns(assignment, 3U, 3U);
}

TEST(HungarianAlgo, WideMatrixKeepsRowCountAndAssignsDistinctColumns) {
    const std::vector<std::vector<float>> costMatrix = {
        {10.0f, 1.0f, 9.0f, 9.0f},
        {10.0f, 9.0f, 1.0f, 9.0f},
    };

    const auto assignment = opk::algo::solveHungarian(costMatrix);

    ASSERT_EQ(assignment.size(), 2U);
    EXPECT_EQ(assignment[0], 1);
    EXPECT_EQ(assignment[1], 2);

    ExpectUniqueAssignedColumns(assignment, 2U, 4U);
}

TEST(HungarianAlgo, TallMatrixUsesTransposePathAndMapsBackToOriginalRows) {
    const std::vector<std::vector<float>> costMatrix = {
        {9.0f, 1.0f},
        {1.0f, 9.0f},
        {2.0f, 2.0f},
        {9.0f, 0.0f},
    };

    const auto assignment = opk::algo::solveHungarian(costMatrix);

    ASSERT_EQ(assignment.size(), 4U);

    EXPECT_EQ(assignment[1], 0);
    EXPECT_EQ(assignment[3], 1);

    size_t unassignedCount = 0;
    for (const int columnIndex : assignment) {
        if (columnIndex < 0) {
            unassignedCount++;
        }
    }
    EXPECT_EQ(unassignedCount, 2U);

    ExpectUniqueAssignedColumns(assignment, 2U, 2U);
}
