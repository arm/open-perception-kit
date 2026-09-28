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

#include <gtest/gtest.h>

#include "perf/PerformanceMetrics.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <vector>

namespace {

using opk::perf::PerformanceMetrics;

static_assert(!std::is_default_constructible_v<PerformanceMetrics>);

std::string tempCsvPath(std::string_view suffix) {
    const auto token = ::getpid();
    const auto filename = std::format("opk_performance_metrics_{}_{}.csv", token, suffix);
    return (std::filesystem::current_path() / filename).string();
}

const PerformanceMetrics::MetricRecord *
findMetric(const PerformanceMetrics::Snapshot &snapshot,
           std::string_view name,
           std::uint64_t parentId = PerformanceMetrics::InvalidMetricId) {
    for (const auto &metric : snapshot.metrics) {
        if (metric.parentId == parentId && metric.getName() == name) {
            return &metric;
        }
    }
    return nullptr;
}

} // namespace

TEST(PerformanceMetrics, NestedScopesProduceHierarchyAndAverages) {
    auto &metrics = opk::perf::defaultPerformanceMetrics();

    {
        auto root = metrics.scope("root");
        std::this_thread::sleep_for(std::chrono::microseconds(20));
        {
            auto child = metrics.scope("child");
            std::this_thread::sleep_for(std::chrono::microseconds(20));
        }
    }

    const auto snapshot = metrics.aggregateSnapshot();
    const auto *root = findMetric(snapshot, "root");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->parentId, PerformanceMetrics::InvalidMetricId);
    EXPECT_EQ(root->depth, 0U);
    EXPECT_EQ(root->count, 1U);
    EXPECT_EQ(root->averageNs, root->totalNs / root->count);
    EXPECT_TRUE(root->hasChildren);

    const auto *child = findMetric(snapshot, "child", root->id);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->depth, 1U);
    EXPECT_EQ(child->count, 1U);
    EXPECT_EQ(child->averageNs, child->totalNs / child->count);
}

TEST(PerformanceMetrics, SnapshotDerivesChildrenForOpenScopes) {
    auto &metrics = opk::perf::defaultPerformanceMetrics();

    auto rootScope = metrics.scope("open-root");
    auto childScope = metrics.scope("open-child");

    const auto snapshot = metrics.aggregateSnapshot();
    const auto *root = findMetric(snapshot, "open-root");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->count, 0U);
    EXPECT_EQ(root->averageNs, 0U);
    EXPECT_TRUE(root->hasChildren);
    EXPECT_EQ(findMetric(snapshot, "open-child", root->id), nullptr);
}

TEST(PerformanceMetrics, SameNameUnderDifferentParentsStaysSeparate) {
    auto &metrics = opk::perf::defaultPerformanceMetrics();

    {
        auto parentA = metrics.scope("parentA");
        auto shared = metrics.scope("shared");
    }
    {
        auto parentB = metrics.scope("parentB");
        auto shared = metrics.scope("shared");
    }

    const auto snapshot = metrics.aggregateSnapshot();
    const auto *parentA = findMetric(snapshot, "parentA");
    const auto *parentB = findMetric(snapshot, "parentB");
    ASSERT_NE(parentA, nullptr);
    ASSERT_NE(parentB, nullptr);

    const auto *sharedA = findMetric(snapshot, "shared", parentA->id);
    const auto *sharedB = findMetric(snapshot, "shared", parentB->id);
    ASSERT_NE(sharedA, nullptr);
    ASSERT_NE(sharedB, nullptr);
    EXPECT_NE(sharedA->id, sharedB->id);
}

TEST(PerformanceMetrics, ScopeIntervalMetricsUseHierarchyAndIncludeNewScopes) {
    auto &metrics = opk::perf::defaultPerformanceMetrics();

    {
        auto parentAScope = metrics.scope("parentA");
        auto sharedScope = metrics.scope("shared");
    }
    {
        auto parentBScope = metrics.scope("parentB");
        auto sharedScope = metrics.scope("shared");
    }
    {
        auto staleScope = metrics.scope("stale");
    }
    const auto intervalStartSnapshot = metrics.aggregateSnapshot();

    for (std::size_t iteration = 0; iteration < 2; ++iteration) {
        auto parentAScope = metrics.scope("parentA");
        auto sharedScope = metrics.scope("shared");
    }
    for (std::size_t iteration = 0; iteration < 3; ++iteration) {
        auto parentBScope = metrics.scope("parentB");
        auto sharedScope = metrics.scope("shared");
    }
    {
        auto newParentScope = metrics.scope("new-parent");
        auto newLeafScope = metrics.scope("new-leaf");
    }
    const auto intervalEndSnapshot = metrics.aggregateSnapshot();

    const auto *intervalStartParentA = findMetric(intervalStartSnapshot, "parentA");
    const auto *intervalStartParentB = findMetric(intervalStartSnapshot, "parentB");
    const auto *intervalEndParentA = findMetric(intervalEndSnapshot, "parentA");
    const auto *intervalEndParentB = findMetric(intervalEndSnapshot, "parentB");
    ASSERT_NE(intervalStartParentA, nullptr);
    ASSERT_NE(intervalStartParentB, nullptr);
    ASSERT_NE(intervalEndParentA, nullptr);
    ASSERT_NE(intervalEndParentB, nullptr);

    const auto *intervalStartSharedA =
        findMetric(intervalStartSnapshot, "shared", intervalStartParentA->id);
    const auto *intervalStartSharedB =
        findMetric(intervalStartSnapshot, "shared", intervalStartParentB->id);
    const auto *intervalEndSharedA =
        findMetric(intervalEndSnapshot, "shared", intervalEndParentA->id);
    const auto *intervalEndSharedB =
        findMetric(intervalEndSnapshot, "shared", intervalEndParentB->id);
    const auto *intervalEndNewParent = findMetric(intervalEndSnapshot, "new-parent");
    ASSERT_NE(intervalEndNewParent, nullptr);
    const auto *intervalEndNewLeaf =
        findMetric(intervalEndSnapshot, "new-leaf", intervalEndNewParent->id);
    ASSERT_NE(intervalStartSharedA, nullptr);
    ASSERT_NE(intervalStartSharedB, nullptr);
    ASSERT_NE(intervalEndSharedA, nullptr);
    ASSERT_NE(intervalEndSharedB, nullptr);
    ASSERT_NE(intervalEndNewLeaf, nullptr);
    ASSERT_GE(intervalEndSharedA->totalNs, intervalStartSharedA->totalNs);
    ASSERT_GE(intervalEndSharedB->totalNs, intervalStartSharedB->totalNs);

    const auto scopeIntervalMetrics =
        opk::perf::calculateScopeIntervalMetrics(intervalStartSnapshot, intervalEndSnapshot);
    std::vector<std::uint64_t> sharedCompletedScopeCounts;
    for (const auto &metric : scopeIntervalMetrics) {
        EXPECT_NE(metric.name, "stale");
        EXPECT_GT(metric.completedScopeCount, 0U);
        EXPECT_EQ(metric.averageDurationNs, metric.totalDurationNs / metric.completedScopeCount);
        if (metric.name == "shared") {
            sharedCompletedScopeCounts.push_back(metric.completedScopeCount);
            if (metric.completedScopeCount == 2) {
                EXPECT_EQ(metric.totalDurationNs,
                          intervalEndSharedA->totalNs - intervalStartSharedA->totalNs);
            } else if (metric.completedScopeCount == 3) {
                EXPECT_EQ(metric.totalDurationNs,
                          intervalEndSharedB->totalNs - intervalStartSharedB->totalNs);
            }
        }
    }
    std::ranges::sort(sharedCompletedScopeCounts);
    EXPECT_EQ(sharedCompletedScopeCounts, (std::vector<std::uint64_t>{2, 3}));

    const auto newParentIntervalMetric = std::ranges::find(
        scopeIntervalMetrics, "new-parent", &opk::perf::ScopeIntervalMetrics::name);
    ASSERT_NE(newParentIntervalMetric, scopeIntervalMetrics.end());
    EXPECT_EQ(newParentIntervalMetric->completedScopeCount, intervalEndNewParent->count);
    EXPECT_EQ(newParentIntervalMetric->totalDurationNs, intervalEndNewParent->totalNs);

    const auto newLeafIntervalMetric =
        std::ranges::find(scopeIntervalMetrics, "new-leaf", &opk::perf::ScopeIntervalMetrics::name);
    ASSERT_NE(newLeafIntervalMetric, scopeIntervalMetrics.end());
    EXPECT_EQ(newLeafIntervalMetric->completedScopeCount, intervalEndNewLeaf->count);
    EXPECT_EQ(newLeafIntervalMetric->totalDurationNs, intervalEndNewLeaf->totalNs);

    auto regressedCounterSnapshot = intervalEndSnapshot;
    for (auto &metric : regressedCounterSnapshot.metrics) {
        metric.count = 0;
        metric.totalNs = 0;
    }
    EXPECT_TRUE(
        opk::perf::calculateScopeIntervalMetrics(intervalStartSnapshot, regressedCounterSnapshot)
            .empty());

    auto regressedDurationSnapshot = intervalEndSnapshot;
    ASSERT_GT(intervalStartParentA->totalNs, 0U);
    regressedDurationSnapshot.metrics[intervalEndParentA->id].totalNs =
        intervalStartParentA->totalNs - 1;
    const auto regressedDurationMetrics =
        opk::perf::calculateScopeIntervalMetrics(intervalStartSnapshot, regressedDurationSnapshot);
    EXPECT_EQ(std::ranges::find(
                  regressedDurationMetrics, "parentA", &opk::perf::ScopeIntervalMetrics::name),
              regressedDurationMetrics.end());
}

TEST(PerformanceMetrics, DefaultRecorderReturnsSingleton) {
    EXPECT_EQ(&opk::perf::defaultPerformanceMetrics(), &opk::perf::defaultPerformanceMetrics());
}

TEST(PerformanceMetrics, ScopeIntervalMetricsIgnoreMalformedHierarchies) {
    PerformanceMetrics::Snapshot invalidParentSnapshot;
    invalidParentSnapshot.metrics.emplace_back();
    invalidParentSnapshot.metrics.front().id = 0;
    invalidParentSnapshot.metrics.front().parentId = 1;
    invalidParentSnapshot.metrics.front().count = 1;

    PerformanceMetrics::Snapshot cyclicHierarchySnapshot;
    cyclicHierarchySnapshot.metrics.emplace_back();
    cyclicHierarchySnapshot.metrics.front().id = 0;
    cyclicHierarchySnapshot.metrics.front().parentId = 0;
    cyclicHierarchySnapshot.metrics.front().count = 1;

    PerformanceMetrics::Snapshot mismatchedParentSnapshot;
    mismatchedParentSnapshot.metrics.resize(2);
    mismatchedParentSnapshot.metrics.front().id = 1;
    mismatchedParentSnapshot.metrics.back().id = 1;
    mismatchedParentSnapshot.metrics.back().parentId = 0;
    mismatchedParentSnapshot.metrics.back().count = 1;

    EXPECT_TRUE(opk::perf::calculateScopeIntervalMetrics({}, invalidParentSnapshot).empty());
    EXPECT_TRUE(opk::perf::calculateScopeIntervalMetrics({}, cyclicHierarchySnapshot).empty());
    EXPECT_TRUE(opk::perf::calculateScopeIntervalMetrics({}, mismatchedParentSnapshot).empty());
}

TEST(PerformanceMetrics, LongNamesAreTruncatedAndReported) {
    auto &metrics = opk::perf::defaultPerformanceMetrics();
    const std::string longName(PerformanceMetrics::MaxSpanNameLength + 8, 'x');

    {
        auto scope = metrics.scope(longName);
    }

    const auto snapshot = metrics.aggregateSnapshot();
    const auto *metric = findMetric(
        snapshot, std::string_view(longName).substr(0, PerformanceMetrics::MaxSpanNameLength));
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->getName().size(), PerformanceMetrics::MaxSpanNameLength);
    EXPECT_TRUE(metric->nameTruncated);
}

TEST(PerformanceMetrics, AggregateOnlyRecordingRetainsCountsWithoutHistory) {
    auto &metrics = opk::perf::defaultPerformanceMetrics();
    metrics.setHistoryEnabled(false);
    const auto initialSpanCount = metrics.snapshot().spans.size();

    for (std::size_t iteration = 0; iteration < 2000; ++iteration) {
        auto scope = metrics.scope("aggregate-only");
    }

    const auto snapshot = metrics.snapshot();
    EXPECT_EQ(snapshot.spans.size(), initialSpanCount);
    const auto *metric = findMetric(snapshot, "aggregate-only");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->count, 2000U);
}

TEST(PerformanceMetrics, HistoryRecordsOnlyCompletedSpans) {
    auto &metrics = opk::perf::defaultPerformanceMetrics();
    metrics.setHistoryEnabled(true);
    const auto firstNewSpan = metrics.snapshot().spans.size();

    {
        auto root = metrics.scope("history-root");
        {
            auto child = metrics.scope("history-child");
        }
    }

    metrics.setHistoryEnabled(false);
    const auto snapshot = metrics.snapshot();
    ASSERT_EQ(snapshot.spans.size(), firstNewSpan + 2);
    const auto &rootSpan = snapshot.spans[firstNewSpan];
    const auto &childSpan = snapshot.spans[firstNewSpan + 1];
    EXPECT_TRUE(rootSpan.complete());
    EXPECT_TRUE(childSpan.complete());
    EXPECT_EQ(rootSpan.durationNs(), rootSpan.endNs - rootSpan.startNs);
    EXPECT_EQ(childSpan.durationNs(), childSpan.endNs - childSpan.startNs);
    EXPECT_EQ(rootSpan.getName(), "history-root");
    EXPECT_EQ(childSpan.getName(), "history-child");
    EXPECT_EQ(childSpan.parentId, rootSpan.id);
}

TEST(PerformanceMetrics, CsvExportWritesCompletedHistoricalSpans) {
    const auto path = tempCsvPath("manual");
    std::remove(path.c_str());

    auto &metrics = opk::perf::defaultPerformanceMetrics();
    metrics.setHistoryEnabled(true);
    {
        auto scope = metrics.scope("csv-root");
    }
    metrics.setHistoryEnabled(false);

    ASSERT_TRUE(metrics.writeCsv(path));

    std::ifstream input(path);
    ASSERT_TRUE(input.good());
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    EXPECT_NE(contents.find("thread_id,span_id,parent_span_id"), std::string::npos);
    EXPECT_NE(contents.find("duration_ms"), std::string::npos);
    EXPECT_EQ(contents.find("name_truncated"), std::string::npos);
    EXPECT_NE(contents.find("\"csv-root\""), std::string::npos);
    std::remove(path.c_str());
}

TEST(PerformanceMetrics, AutoCsvExportWritesConfiguredPath) {
    const auto path = tempCsvPath("auto");
    std::remove(path.c_str());

    auto &metrics = opk::perf::defaultPerformanceMetrics();
    metrics.setHistoryEnabled(true);
    metrics.setAutoCsvExportPath(path);
    {
        auto scope = metrics.scope("auto-root");
    }
    metrics.setHistoryEnabled(false);
    metrics.writeAutoCsv();
    metrics.setAutoCsvExportPath({});

    std::ifstream input(path);
    ASSERT_TRUE(input.good());
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    EXPECT_NE(contents.find("\"auto-root\""), std::string::npos);
    std::remove(path.c_str());
}

TEST(PerformanceMetrics, SnapshotWhileThreadsRecordIsSafe) {
    auto &metrics = opk::perf::defaultPerformanceMetrics();
    std::atomic<bool> start{false};
    {
        std::vector<std::jthread> threads;
        threads.reserve(8);

        for (std::size_t index = 0; index < 8; ++index) {
            threads.emplace_back([&metrics, &start]() {
                while (!start.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                for (std::size_t iteration = 0; iteration < 500; ++iteration) {
                    auto outer = metrics.scope("outer");
                    auto inner = metrics.scope("inner");
                }
            });
        }

        start.store(true, std::memory_order_release);
        for (std::size_t iteration = 0; iteration < 50; ++iteration) {
            (void)metrics.aggregateSnapshot();
        }
    }

    const auto snapshot = metrics.aggregateSnapshot();
    const auto *outer = findMetric(snapshot, "outer");
    ASSERT_NE(outer, nullptr);
    EXPECT_GT(outer->count, 0U);
}

TEST(PerformanceMetrics, ThreadSlotOverflowIsReportedSafely) {
    auto &metrics = opk::perf::defaultPerformanceMetrics();
    {
        std::vector<std::jthread> threads;
        threads.reserve(132);

        for (std::size_t index = 0; index < 132; ++index) {
            threads.emplace_back([&metrics]() {
                auto scope = metrics.scope("threaded");
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            });
        }
    }

    const auto snapshot = metrics.aggregateSnapshot();
    EXPECT_TRUE(snapshot.threadSlotOverflow);
}
