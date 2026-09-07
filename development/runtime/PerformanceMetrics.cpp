/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "runtime/PerformanceMetrics.h"

#include "perf/PerformanceMetrics.h"

#include <utility>

namespace pek::runtime {

void PerformanceMetrics::setHistoryEnabled(bool enabled) {
    pek::perf::defaultPerformanceMetrics().setHistoryEnabled(enabled);
}

bool PerformanceMetrics::historyEnabled() {
    return pek::perf::defaultPerformanceMetrics().historyEnabled();
}

void PerformanceMetrics::setAutoCsvExportPath(const std::string &path) {
    pek::perf::defaultPerformanceMetrics().setAutoCsvExportPath(path);
}

bool PerformanceMetrics::writeCsv(const std::string &path) {
    return pek::perf::defaultPerformanceMetrics().writeCsv(path);
}

PerformanceMetricsSnapshot PerformanceMetrics::snapshot() {
    const auto source = pek::perf::defaultPerformanceMetrics().snapshot();

    PerformanceMetricsSnapshot result;
    result.droppedMetrics = source.droppedMetrics;
    result.droppedSpans = source.droppedSpans;
    result.droppedHistoryEvents = source.droppedHistoryEvents;
    result.wrongThreadScopeCloses = source.wrongThreadScopeCloses;
    result.threadSlotOverflow = source.threadSlotOverflow;
    result.metrics.reserve(source.metrics.size());
    result.spans.reserve(source.spans.size());

    for (const auto &metric : source.metrics) {
        PerformanceMetric output;
        output.id = metric.id;
        output.parentId = metric.parentId;
        output.name = std::string(metric.getName());
        output.depth = metric.depth;
        output.count = metric.count;
        output.totalNs = metric.totalNs;
        output.averageNs = metric.averageNs;
        output.minNs = metric.minNs;
        output.maxNs = metric.maxNs;
        output.lastNs = metric.lastNs;
        output.nameTruncated = metric.nameTruncated;
        output.hasChildren = metric.hasChildren;

        result.metrics.push_back(std::move(output));
    }

    for (const auto &span : source.spans) {
        PerformanceSpan output;
        output.id = span.id;
        output.parentId = span.parentId;
        output.name = std::string(span.getName());
        output.startNs = span.startNs;
        output.endNs = span.endNs;
        output.durationNs = span.durationNs();
        output.threadId = span.threadId;
        output.depth = span.depth;
        output.complete = true;
        output.nameTruncated = span.nameTruncated;

        result.spans.push_back(std::move(output));
    }

    return result;
}

} // namespace pek::runtime
