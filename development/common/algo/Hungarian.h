/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

#pragma once

#include <cstddef>
#include <limits>
#include <type_traits>
#include <vector>

namespace opk::algo {

template <typename T>
std::vector<int> solveHungarian(const std::vector<std::vector<T>> &inputCost) {
    static_assert(std::is_arithmetic_v<T>, "Hungarian solver expects arithmetic cost type");

    if (inputCost.empty() || inputCost.front().empty()) {
        return {};
    }

    const size_t originalRows = inputCost.size();
    const size_t originalCols = inputCost.front().size();

    // Validate that all rows have the same number of columns as the first row.
    // If input is ragged, bail out to avoid out-of-bounds indexing later.
    for (size_t r = 0; r < originalRows; ++r) {
        if (inputCost[r].size() != originalCols) {
            return {};
        }
    }

    bool transposed = false;
    std::vector<std::vector<T>> cost = inputCost;

    if (originalRows > originalCols) {
        transposed = true;
        cost.assign(originalCols, std::vector<T>(originalRows, T(0)));
        for (size_t r = 0; r < originalRows; ++r) {
            for (size_t c = 0; c < originalCols; ++c) {
                cost[c][r] = inputCost[r][c];
            }
        }
    }

    const auto n = static_cast<int>(cost.size());
    const auto m = static_cast<int>(cost.front().size());

    std::vector<T> rowPotentials(n + 1, T(0));
    std::vector<T> colPotentials(m + 1, T(0));
    std::vector<int> matchedRowByCol(m + 1, 0);
    std::vector<int> predecessorCol(m + 1, 0);

    for (int i = 1; i <= n; ++i) {
        matchedRowByCol[0] = i;
        int currentCol = 0;
        std::vector<T> minSlackByCol(m + 1, std::numeric_limits<T>::max());
        std::vector<bool> isColInAlternatingTree(m + 1, false);

        do {
            isColInAlternatingTree[currentCol] = true;
            const int currentRow = matchedRowByCol[currentCol];
            T minPotentialAdjustment = std::numeric_limits<T>::max();
            int nextCol = 0;

            for (int j = 1; j <= m; ++j) {
                if (isColInAlternatingTree[j]) {
                    continue;
                }

                const T reducedCost =
                    cost[static_cast<size_t>(currentRow - 1)][static_cast<size_t>(j - 1)] -
                    rowPotentials[currentRow] - colPotentials[j];
                if (reducedCost < minSlackByCol[j]) {
                    minSlackByCol[j] = reducedCost;
                    predecessorCol[j] = currentCol;
                }
                if (minSlackByCol[j] < minPotentialAdjustment) {
                    minPotentialAdjustment = minSlackByCol[j];
                    nextCol = j;
                }
            }

            for (int j = 0; j <= m; ++j) {
                if (isColInAlternatingTree[j]) {
                    rowPotentials[matchedRowByCol[j]] += minPotentialAdjustment;
                    colPotentials[j] -= minPotentialAdjustment;
                } else {
                    minSlackByCol[j] -= minPotentialAdjustment;
                }
            }

            currentCol = nextCol;
        } while (matchedRowByCol[currentCol] != 0);

        do {
            const int previousCol = predecessorCol[currentCol];
            matchedRowByCol[currentCol] = matchedRowByCol[previousCol];
            currentCol = previousCol;
        } while (currentCol != 0);
    }

    std::vector<int> assignmentRows(static_cast<size_t>(n), -1);
    for (int j = 1; j <= m; ++j) {
        if (matchedRowByCol[j] != 0) {
            assignmentRows[static_cast<size_t>(matchedRowByCol[j] - 1)] = j - 1;
        }
    }

    if (!transposed) {
        return assignmentRows;
    }

    std::vector<int> assignmentOriginalRows(originalRows, -1);
    for (size_t transposedRow = 0; transposedRow < assignmentRows.size(); ++transposedRow) {
        const int transposedCol = assignmentRows[transposedRow];
        if (transposedCol >= 0) {
            assignmentOriginalRows[static_cast<size_t>(transposedCol)] =
                static_cast<int>(transposedRow);
        }
    }

    return assignmentOriginalRows;
}

} // namespace opk::algo
