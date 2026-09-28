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

#include "runtime/PerformanceMetrics.h"

#include "perf/PerformanceMetrics.h"

#include <utility>

namespace opk::runtime {

void PerformanceMetrics::setHistoryEnabled(bool enabled) {
    opk::perf::defaultPerformanceMetrics().setHistoryEnabled(enabled);
}

bool PerformanceMetrics::historyEnabled() {
    return opk::perf::defaultPerformanceMetrics().historyEnabled();
}

void PerformanceMetrics::setAutoCsvExportPath(const std::string &path) {
    opk::perf::defaultPerformanceMetrics().setAutoCsvExportPath(path);
}

bool PerformanceMetrics::writeCsv(const std::string &path) {
    return opk::perf::defaultPerformanceMetrics().writeCsv(path);
}

PerformanceMetricsSnapshot PerformanceMetrics::snapshot() {
    const auto source = opk::perf::defaultPerformanceMetrics().snapshot();

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

} // namespace opk::runtime
