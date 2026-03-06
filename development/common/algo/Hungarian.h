/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <cstddef>
#include <limits>
#include <type_traits>
#include <vector>

namespace amp::algo {

template <typename T>
std::vector<int> solveHungarian(const std::vector<std::vector<T>> &inputCost) {
    static_assert(std::is_arithmetic_v<T>, "Hungarian solver expects arithmetic cost type");

    if (inputCost.empty() || inputCost.front().empty()) {
        return {};
    }

    const size_t originalRows = inputCost.size();
    const size_t originalCols = inputCost.front().size();

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

    std::vector<T> u(n + 1, T(0));
    std::vector<T> v(m + 1, T(0));
    std::vector<int> p(m + 1, 0);
    std::vector<int> way(m + 1, 0);

    for (int i = 1; i <= n; ++i) {
        p[0] = i;
        int j0 = 0;
        std::vector<T> minv(m + 1, std::numeric_limits<T>::max());
        std::vector<bool> used(m + 1, false);

        do {
            used[j0] = true;
            const int i0 = p[j0];
            T delta = std::numeric_limits<T>::max();
            int j1 = 0;

            for (int j = 1; j <= m; ++j) {
                if (used[j]) {
                    continue;
                }

                const T cur =
                    cost[static_cast<size_t>(i0 - 1)][static_cast<size_t>(j - 1)] - u[i0] - v[j];
                if (cur < minv[j]) {
                    minv[j] = cur;
                    way[j] = j0;
                }
                if (minv[j] < delta) {
                    delta = minv[j];
                    j1 = j;
                }
            }

            for (int j = 0; j <= m; ++j) {
                if (used[j]) {
                    u[p[j]] += delta;
                    v[j] -= delta;
                } else {
                    minv[j] -= delta;
                }
            }

            j0 = j1;
        } while (p[j0] != 0);

        do {
            const int j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0 != 0);
    }

    std::vector<int> assignmentRows(static_cast<size_t>(n), -1);
    for (int j = 1; j <= m; ++j) {
        if (p[j] != 0) {
            assignmentRows[static_cast<size_t>(p[j] - 1)] = j - 1;
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

} // namespace amp::algo
