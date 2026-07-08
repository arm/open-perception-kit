/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pek::runtime {

/**
 * @brief Aggregated timings for one performance scope name.
 */
struct PerformanceMetric {
    std::string name;
    std::uint32_t depth = 0;
    std::uint64_t count = 0;
    std::uint64_t totalNs = 0;
    std::uint64_t minNs = 0;
    std::uint64_t maxNs = 0;
    std::uint64_t lastNs = 0;
    bool nameTruncated = false;
    bool hasChildren = false;
};

/**
 * @brief One recorded performance span.
 *
 * Spans are process-wide PEK measurements collected by the common performance
 * metrics recorder. They are not owned by one runtime::OpChain instance.
 */
struct PerformanceSpan {
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
 * @brief Process-wide snapshot of collected PEK performance metrics.
 */
struct PerformanceMetricsSnapshot {
    std::vector<PerformanceMetric> metrics;
    std::vector<PerformanceSpan> spans;
    std::uint32_t droppedMetrics = 0;
    std::uint32_t droppedSpans = 0;
    bool threadSlotOverflow = false;
};

/**
 * @brief Runtime facade for process-wide PEK performance metrics.
 *
 * This API exposes metrics collected by PEK_PERF_SCOPE instrumentation without
 * requiring external applications to include common/perf headers.
 */
class PerformanceMetrics {
  public:
    /**
     * @brief Clears all process-wide collected performance metrics.
     */
    static void reset();

    /**
     * @brief Enables or disables exact span trace collection.
     *
     * Aggregate metrics are collected while the common recorder is enabled.
     * Exact spans are only stored when trace collection is explicitly enabled.
     */
    static void setTraceEnabled(bool enabled);

    /**
     * @brief Returns whether exact span trace collection is enabled.
     */
    static bool traceEnabled();

    /**
     * @brief Returns a copy of the currently collected process-wide metrics.
     */
    static PerformanceMetricsSnapshot snapshot();
};

} // namespace pek::runtime
