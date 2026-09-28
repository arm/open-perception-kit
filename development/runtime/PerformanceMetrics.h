/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

#include <cstdint>
#include <string>
#include <vector>

namespace opk::runtime {

/**
 * @brief Aggregated timings for one performance scope name.
 */
struct PerformanceMetric {
    std::uint64_t id = 0;
    std::uint64_t parentId = 0;
    std::string name;
    std::uint32_t depth = 0;
    std::uint64_t count = 0;
    std::uint64_t totalNs = 0;
    std::uint64_t averageNs = 0;
    std::uint64_t minNs = 0;
    std::uint64_t maxNs = 0;
    std::uint64_t lastNs = 0;
    bool nameTruncated = false;
    bool hasChildren = false;
};

/**
 * @brief One recorded performance span.
 *
 * Spans are process-wide OPK measurements collected by the common performance
 * metrics recorder. They are not owned by one runtime::OpChain instance.
 */
struct PerformanceSpan {
    std::uint64_t id = 0;
    std::uint64_t parentId = 0;
    std::string name;
    std::uint64_t startNs = 0;
    std::uint64_t endNs = 0;
    std::uint64_t durationNs = 0;
    std::uint64_t threadId = 0;
    std::uint32_t depth = 0;
    bool complete = false;
    bool nameTruncated = false;
};

/**
 * @brief Process-wide snapshot of collected OPK performance metrics.
 *
 * The vectors in the snapshot own copies of the metric records present when
 * snapshot() was called.
 */
struct PerformanceMetricsSnapshot {
    std::vector<PerformanceMetric> metrics;
    std::vector<PerformanceSpan> spans;
    std::uint32_t droppedMetrics = 0;
    std::uint32_t droppedSpans = 0;
    std::uint32_t droppedHistoryEvents = 0;
    std::uint32_t wrongThreadScopeCloses = 0;
    bool threadSlotOverflow = false;
};

/**
 * @brief Runtime facade for process-wide OPK performance metrics.
 *
 * This API exposes metrics collected by OPK_PERF_SCOPE instrumentation without
 * requiring external applications to include common/perf headers.
 *
 * Metrics configuration and collected data are process-wide. They are not owned
 * by, or scoped to, one Pipeline or OpChain instance.
 */
class PerformanceMetrics {
  public:
    /**
     * @brief Enables or disables historical completed-span collection.
     *
     * Aggregate metrics are always collected. Historical spans are only stored
     * when history collection is explicitly enabled.
     */
    static void setHistoryEnabled(bool enabled);

    /**
     * @brief Returns whether historical completed-span collection is enabled.
     */
    static bool historyEnabled();

    /**
     * @brief Sets the optional best-effort CSV export path for normal process shutdown.
     */
    static void setAutoCsvExportPath(const std::string &path);

    /**
     * @brief Explicitly writes completed historical spans to CSV.
     */
    static bool writeCsv(const std::string &path);

    /**
     * @brief Returns a copy of the currently collected process-wide metrics.
     */
    static PerformanceMetricsSnapshot snapshot();
};

} // namespace opk::runtime
