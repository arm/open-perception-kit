/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include "perf/PerformanceMetrics.h"

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
        if (metric.parentId == parentId && metric.nameView() == name) {
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

TEST(PerformanceMetrics, LongNamesAreTruncatedAndReported) {
    PerformanceMetrics metrics;
    const std::string longName(PerformanceMetrics::MaxSpanNameLength + 8, 'x');

    {
        auto scope = metrics.scope(longName);
    }

    const auto snapshot = metrics.aggregateSnapshot();
    ASSERT_EQ(snapshot.metrics.size(), 1U);
    EXPECT_EQ(snapshot.metrics.front().nameView().size(), PerformanceMetrics::MaxSpanNameLength);
    EXPECT_TRUE(snapshot.metrics.front().nameTruncated);
}

TEST(PerformanceMetrics, HistoryDisabledStoresNoSpans) {
    PerformanceMetrics metrics;

    {
        auto scope = metrics.scope("aggregate-only");
    }

    const auto snapshot = metrics.snapshot();
    EXPECT_TRUE(snapshot.spans.empty());
    ASSERT_EQ(snapshot.metrics.size(), 1U);
    EXPECT_EQ(snapshot.metrics.front().count, 1U);
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
    EXPECT_EQ(snapshot.spans[0].nameView(), "history-root");
    EXPECT_EQ(snapshot.spans[1].nameView(), "history-child");
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
