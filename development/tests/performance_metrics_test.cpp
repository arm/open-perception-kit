/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

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
#include <unistd.h>
#include <vector>

namespace {

using pek::perf::PerformanceMetrics;

std::string tempCsvPath(std::string_view suffix) {
    const auto token = ::getpid();
    const auto filename = std::format("pek_performance_metrics_{}_{}.csv", token, suffix);
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
    PerformanceMetrics metrics;

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
    EXPECT_GT(root->averageNs, 0U);
    EXPECT_TRUE(root->hasChildren);

    const auto *child = findMetric(snapshot, "child", root->id);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->depth, 1U);
    EXPECT_EQ(child->count, 1U);
    EXPECT_GT(child->averageNs, 0U);
}

TEST(PerformanceMetrics, SameNameUnderDifferentParentsStaysSeparate) {
    PerformanceMetrics metrics;

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
    PerformanceMetrics metrics;

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
        pek::perf::calculateScopeIntervalMetrics(intervalStartSnapshot, intervalEndSnapshot);
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
        scopeIntervalMetrics, "new-parent", &pek::perf::ScopeIntervalMetrics::name);
    ASSERT_NE(newParentIntervalMetric, scopeIntervalMetrics.end());
    EXPECT_EQ(newParentIntervalMetric->completedScopeCount, intervalEndNewParent->count);
    EXPECT_EQ(newParentIntervalMetric->totalDurationNs, intervalEndNewParent->totalNs);

    const auto newLeafIntervalMetric =
        std::ranges::find(scopeIntervalMetrics, "new-leaf", &pek::perf::ScopeIntervalMetrics::name);
    ASSERT_NE(newLeafIntervalMetric, scopeIntervalMetrics.end());
    EXPECT_EQ(newLeafIntervalMetric->completedScopeCount, intervalEndNewLeaf->count);
    EXPECT_EQ(newLeafIntervalMetric->totalDurationNs, intervalEndNewLeaf->totalNs);

    auto regressedCounterSnapshot = intervalEndSnapshot;
    for (auto &metric : regressedCounterSnapshot.metrics) {
        metric.count = 0;
        metric.totalNs = 0;
    }
    EXPECT_TRUE(
        pek::perf::calculateScopeIntervalMetrics(intervalStartSnapshot, regressedCounterSnapshot)
            .empty());

    auto regressedDurationSnapshot = intervalEndSnapshot;
    ASSERT_GT(intervalStartParentA->totalNs, 0U);
    regressedDurationSnapshot.metrics[intervalEndParentA->id].totalNs =
        intervalStartParentA->totalNs - 1;
    const auto regressedDurationMetrics =
        pek::perf::calculateScopeIntervalMetrics(intervalStartSnapshot, regressedDurationSnapshot);
    EXPECT_EQ(std::ranges::find(
                  regressedDurationMetrics, "parentA", &pek::perf::ScopeIntervalMetrics::name),
              regressedDurationMetrics.end());
}

TEST(PerformanceMetrics, SwitchingRecordersPreservesEachNestedStack) {
    PerformanceMetrics first;
    PerformanceMetrics second;

    {
        auto firstRoot = first.scope("first-root");
        {
            auto secondRoot = second.scope("second-root");
        }
        {
            auto firstChild = first.scope("first-child");
        }
    }

    const auto firstSnapshot = first.aggregateSnapshot();
    const auto *firstRoot = findMetric(firstSnapshot, "first-root");
    ASSERT_NE(firstRoot, nullptr);
    EXPECT_NE(findMetric(firstSnapshot, "first-child", firstRoot->id), nullptr);

    const auto secondSnapshot = second.aggregateSnapshot();
    EXPECT_NE(findMetric(secondSnapshot, "second-root"), nullptr);
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

    EXPECT_TRUE(pek::perf::calculateScopeIntervalMetrics({}, invalidParentSnapshot).empty());
    EXPECT_TRUE(pek::perf::calculateScopeIntervalMetrics({}, cyclicHierarchySnapshot).empty());
    EXPECT_TRUE(pek::perf::calculateScopeIntervalMetrics({}, mismatchedParentSnapshot).empty());
}

TEST(PerformanceMetrics, LongNamesAreTruncatedAndReported) {
    PerformanceMetrics metrics;
    const std::string longName(PerformanceMetrics::MaxSpanNameLength + 8, 'x');

    {
        auto scope = metrics.scope(longName);
    }

    const auto snapshot = metrics.aggregateSnapshot();
    ASSERT_EQ(snapshot.metrics.size(), 1U);
    EXPECT_EQ(snapshot.metrics.front().getName().size(), PerformanceMetrics::MaxSpanNameLength);
    EXPECT_TRUE(snapshot.metrics.front().nameTruncated);
}

TEST(PerformanceMetrics, AggregateOnlyRecordingRetainsCountsWithoutHistory) {
    PerformanceMetrics metrics;

    for (std::size_t iteration = 0; iteration < 2000; ++iteration) {
        auto scope = metrics.scope("aggregate-only");
    }

    const auto snapshot = metrics.snapshot();
    EXPECT_TRUE(snapshot.spans.empty());
    ASSERT_EQ(snapshot.metrics.size(), 1U);
    EXPECT_EQ(snapshot.metrics.front().count, 2000U);
}

TEST(PerformanceMetrics, HistoryRecordsOnlyCompletedSpans) {
    PerformanceMetrics metrics;
    metrics.setHistoryEnabled(true);

    {
        auto root = metrics.scope("history-root");
        {
            auto child = metrics.scope("history-child");
        }
    }

    const auto snapshot = metrics.snapshot();
    ASSERT_EQ(snapshot.spans.size(), 2U);
    EXPECT_TRUE(snapshot.spans[0].complete());
    EXPECT_TRUE(snapshot.spans[1].complete());
    EXPECT_EQ(snapshot.spans[0].getName(), "history-root");
    EXPECT_EQ(snapshot.spans[1].getName(), "history-child");
    EXPECT_EQ(snapshot.spans[1].parentId, snapshot.spans[0].id);
}

TEST(PerformanceMetrics, CsvExportWritesCompletedHistoricalSpans) {
    const auto path = tempCsvPath("manual");
    std::remove(path.c_str());

    PerformanceMetrics metrics;
    metrics.setHistoryEnabled(true);
    {
        auto scope = metrics.scope("csv-root");
    }

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

TEST(PerformanceMetrics, AutoCsvExportWritesOnNormalDestruction) {
    const auto path = tempCsvPath("auto");
    std::remove(path.c_str());

    {
        PerformanceMetrics metrics;
        metrics.setHistoryEnabled(true);
        metrics.setAutoCsvExportPath(path);
        {
            auto scope = metrics.scope("auto-root");
        }
    }

    std::ifstream input(path);
    ASSERT_TRUE(input.good());
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    EXPECT_NE(contents.find("\"auto-root\""), std::string::npos);
    std::remove(path.c_str());
}

TEST(PerformanceMetrics, ThreadSlotOverflowIsReportedSafely) {
    PerformanceMetrics metrics;
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

TEST(PerformanceMetrics, SnapshotWhileThreadsRecordIsSafe) {
    PerformanceMetrics metrics;
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
