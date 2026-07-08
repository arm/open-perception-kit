/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "runtime/PerformanceMetrics.h"

#include "perf/PerformanceMetrics.h"

#include <utility>

namespace pek::runtime {

void PerformanceMetrics::reset() {
    pek::perf::defaultPerformanceMetrics().reset();
}

void PerformanceMetrics::setTraceEnabled(bool enabled) {
    pek::perf::defaultPerformanceMetrics().setTraceEnabled(enabled);
}

bool PerformanceMetrics::traceEnabled() {
    return pek::perf::defaultPerformanceMetrics().traceEnabled();
}

PerformanceMetricsSnapshot PerformanceMetrics::snapshot() {
    const auto source = pek::perf::defaultPerformanceMetrics().snapshot();

    PerformanceMetricsSnapshot result;
    result.droppedMetrics = source.droppedMetrics;
    result.droppedSpans = source.droppedSpans;
    result.threadSlotOverflow = source.threadSlotOverflow;
    result.metrics.reserve(source.metrics.size());
    result.spans.reserve(source.spans.size());

    for (const auto &metric : source.metrics) {
        PerformanceMetric output;
        output.name = std::string(metric.nameView());
        output.depth = metric.depth;
        output.count = metric.count;
        output.totalNs = metric.totalNs;
        output.minNs = metric.minNs;
        output.maxNs = metric.maxNs;
        output.lastNs = metric.lastNs;
        output.nameTruncated = metric.nameTruncated;
        output.hasChildren = metric.hasChildren;

        result.metrics.push_back(std::move(output));
    }

    for (const auto &span : source.spans) {
        PerformanceSpan output;
        output.name = std::string(span.nameView());
        output.startNs = span.startNs;
        output.endNs = span.endNs;
        output.durationNs = span.durationNs();
        output.threadId = span.threadId;
        output.depth = span.depth;
        output.complete = span.complete();
        output.nameTruncated = span.nameTruncated;

        result.spans.push_back(std::move(output));
    }

    return result;
}

} // namespace pek::runtime
