/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace opk::perf {

namespace detail {
struct PerformanceMetricsState;
using PerformanceMetricsStatePtr = std::unique_ptr<PerformanceMetricsState>;
} // namespace detail

/**
 * Process-wide hierarchical block timer for OPK instrumentation.
 *
 * The default mode records aggregate timing only. Scope names are copied into a
 * fixed-size inline buffer, so callers may pass dynamic string_views without
 * tying metric identity to the lifetime of a const char* literal.
 *
 * Historical capture is explicit opt-in. When enabled, completed scope calls
 * are stored for CSV export. Open scopes are never fabricated during export.
 *
 * Scope objects must be destroyed on the same thread where they were created.
 */
class PerformanceMetrics {
  public:
    /** Sentinel value used when a historical span has no valid identifier. */
    static constexpr std::uint64_t InvalidSpanId = std::numeric_limits<std::uint64_t>::max();

    /** Sentinel value used when an aggregate metric has no parent metric. */
    static constexpr std::uint64_t InvalidMetricId = std::numeric_limits<std::uint64_t>::max();

    /** Maximum copied scope-name length, excluding the terminating null byte. */
    static constexpr std::size_t MaxSpanNameLength = 63;

    using SpanName = std::array<char, MaxSpanNameLength + 1>;

    /**
     * One completed historical scope lifetime.
     *
     * Span records are produced only when historical capture is enabled before a
     * scope exits. Incomplete scopes are not exported or included in snapshots.
     */
    struct SpanRecord {
        /** Unique process-local span identifier assigned at scope entry. */
        std::uint64_t id = InvalidSpanId;

        /** Parent span identifier, or InvalidSpanId for a root span. */
        std::uint64_t parentId = InvalidSpanId;

        /** Copied, null-terminated scope name. */
        SpanName name{};

        /** Monotonic start timestamp in nanoseconds. */
        std::uint64_t startNs = 0;

        /** Monotonic end timestamp in nanoseconds. */
        std::uint64_t endNs = 0;

        /** Linux thread identifier of the thread that recorded this span. */
        std::uint64_t threadId = 0;

        /** Nesting depth at which this span was opened. */
        std::uint32_t depth = 0;

        /** True when the original scope name exceeded MaxSpanNameLength. */
        bool nameTruncated = false;

        /** Returns the copied scope name as a string_view. */
        [[nodiscard]] std::string_view getName() const noexcept {
            return name.data();
        }

        /** Returns true when the span has a valid end timestamp. */
        [[nodiscard]] bool complete() const noexcept {
            return endNs >= startNs && endNs != 0;
        }

        /** Returns the span duration in nanoseconds, or zero when incomplete. */
        [[nodiscard]] std::uint64_t durationNs() const noexcept {
            return complete() ? endNs - startNs : 0;
        }
    };

    /**
     * Aggregated timing for one hierarchical scope path.
     *
     * Metrics are keyed by parent path and copied name, so the same name under
     * two different parents produces two distinct records.
     */
    struct MetricRecord {
        /** Snapshot-local metric identifier. Stable only within one snapshot. */
        std::uint64_t id = InvalidMetricId;

        /** Parent metric identifier, or InvalidMetricId for a root metric. */
        std::uint64_t parentId = InvalidMetricId;

        /** Copied, null-terminated scope name. */
        SpanName name{};

        /** Nesting depth of this metric path. */
        std::uint32_t depth = 0;

        /** Number of completed scopes merged into this metric. */
        std::uint64_t count = 0;

        /** Sum of all completed scope durations in nanoseconds. */
        std::uint64_t totalNs = 0;

        /** Average completed scope duration in nanoseconds. */
        std::uint64_t averageNs = 0;

        /** Smallest completed scope duration in nanoseconds. */
        std::uint64_t minNs = 0;

        /** Largest completed scope duration in nanoseconds. */
        std::uint64_t maxNs = 0;

        /** Most recently recorded completed scope duration in nanoseconds. */
        std::uint64_t lastNs = 0;

        /** True when any merged scope name exceeded MaxSpanNameLength. */
        bool nameTruncated = false;

        /** True when this metric has at least one child metric. */
        bool hasChildren = false;

        /** Returns the copied scope name as a string_view. */
        [[nodiscard]] std::string_view getName() const noexcept {
            return name.data();
        }
    };

    /**
     * Point-in-time copy safe to read outside the recorder.
     *
     * aggregateSnapshot() fills metrics only. snapshot() also includes completed
     * historical spans when history capture has been enabled.
     */
    struct Snapshot {
        /** Hierarchical aggregate metrics collected across thread slots. */
        std::vector<MetricRecord> metrics;

        /** Completed historical spans copied from per-thread history buffers. */
        std::vector<SpanRecord> spans;

        /** Number of aggregate metric records dropped because capacity was exhausted. */
        std::uint32_t droppedMetrics = 0;

        /** Number of scopes dropped because stack depth was exhausted. */
        std::uint32_t droppedSpans = 0;

        /** Number of historical events dropped because chunk allocation failed. */
        std::uint32_t droppedHistoryEvents = 0;

        /** Number of scope closes ignored because they ran on the wrong thread. */
        std::uint32_t wrongThreadScopeCloses = 0;

        /** True when more threads attempted to record than the recorder supports. */
        bool threadSlotOverflow = false;
    };

    /**
     * RAII token returned by scope().
     *
     * Destroying or explicitly closing the token records elapsed time. A Scope
     * must be closed on the same thread where it was created.
     */
    class Scope {
      public:
        /** Constructs an inactive scope. */
        Scope() noexcept = default;

        /** Scope tokens are move-only to avoid double-closing a measurement. */
        Scope(const Scope &) = delete;

        /** Scope tokens are move-only to avoid double-closing a measurement. */
        Scope &operator=(const Scope &) = delete;

        /** Transfers ownership of an active measurement. */
        Scope(Scope &&other) noexcept;

        /** Closes this measurement, then transfers ownership from another scope. */
        Scope &operator=(Scope &&other) noexcept;

        /** Closes the measurement if it is still active. */
        ~Scope();

        /** Closes the measurement early. Safe to call more than once. */
        void close() noexcept;

        /** Returns true while this token owns an active measurement. */
        [[nodiscard]] bool active() const noexcept {
            return recording.metrics != nullptr;
        }

      private:
        friend class PerformanceMetrics;

        struct Recording {
            PerformanceMetrics *metrics = nullptr;
            std::uint32_t slotIndex = 0;
            std::uint32_t metricIndex = 0;
            std::uint64_t spanId = InvalidSpanId;
            std::uint64_t parentSpanId = InvalidSpanId;
            std::uint64_t startNs = 0;
            std::uint32_t depth = 0;
            bool historyRecorded = false;
        };

        explicit Scope(const Recording &recording) noexcept;

        Recording recording;
    };

    /**
     * Starts measuring a named scope.
     *
     * The name is copied immediately into fixed-size storage. Returns an
     * inactive Scope when capacity is exhausted.
     */
    [[nodiscard]] Scope scope(std::string_view name) noexcept;

    /** Enables or disables storing completed spans for snapshots and CSV export. */
    void setHistoryEnabled(bool enabled) noexcept;

    /** Returns whether completed span history is currently recorded. */
    [[nodiscard]] bool historyEnabled() const noexcept;

    /**
     * Sets the optional best-effort automatic CSV export path.
     *
     * The recorder writes this path during normal destruction or process-exit
     * handling for the default recorder. Automatic export is best effort and
     * includes completed historical spans only.
     */
    void setAutoCsvExportPath(std::string_view path);

    /** Returns the currently configured automatic CSV export path. */
    [[nodiscard]] std::string autoCsvExportPath() const;

    /**
     * Writes completed historical spans to CSV.
     *
     * Open scopes are omitted rather than assigned fabricated end timestamps.
     * Returns false when the file cannot be opened or flushed successfully.
     */
    [[nodiscard]] bool writeCsv(std::string_view path) const;

    /** Writes the configured automatic CSV path and reports failures to standard error. */
    void writeAutoCsv() const noexcept;

    /** Returns aggregate metrics without copying historical span records. */
    [[nodiscard]] Snapshot aggregateSnapshot() const;

    /** Returns aggregate metrics and completed historical span records. */
    [[nodiscard]] Snapshot snapshot() const;

  private:
    friend PerformanceMetrics &defaultPerformanceMetrics() noexcept;

    PerformanceMetrics();
    ~PerformanceMetrics();

    PerformanceMetrics(const PerformanceMetrics &) = delete;
    PerformanceMetrics &operator=(const PerformanceMetrics &) = delete;
    PerformanceMetrics(PerformanceMetrics &&) = delete;
    PerformanceMetrics &operator=(PerformanceMetrics &&) = delete;

    void exitBlock(std::uint32_t slotIndex,
                   std::uint32_t metricIndex,
                   std::uint64_t spanId,
                   std::uint64_t parentSpanId,
                   std::uint64_t startNs,
                   std::uint32_t depth,
                   bool historyRecorded) noexcept;

    detail::PerformanceMetricsStatePtr state;
};

/** Timing statistics for one scope completed between two aggregate snapshots. */
struct ScopeIntervalMetrics {
    /** Copied name of the scope represented by this interval. */
    std::string name;

    /** Number of scopes completed during the interval. */
    std::uint64_t completedScopeCount = 0;

    /** Total duration of scopes completed during the interval, in nanoseconds. */
    std::uint64_t totalDurationNs = 0;

    /** Average duration of scopes completed during the interval, in nanoseconds. */
    std::uint64_t averageDurationNs = 0;
};

/**
 * Calculates timing statistics for scopes completed between chronological snapshots.
 *
 * Metrics are matched by their complete root-to-scope name hierarchy. Missing hierarchies and
 * records whose counters moved backwards are omitted.
 *
 * The hierarchy is the dynamic nesting of named recorder scopes, not a filesystem path or a
 * reconstructed static call graph.
 */
[[nodiscard]] std::vector<ScopeIntervalMetrics>
calculateScopeIntervalMetrics(const PerformanceMetrics::Snapshot &intervalStartSnapshot,
                              const PerformanceMetrics::Snapshot &intervalEndSnapshot);

/**
 * Lazy process-wide metrics recorder for no-init experiments. The recorder is
 * destroyed normally at process shutdown, including best-effort automatic CSV
 * export when configured, without leaking the process-wide recorder.
 */
[[nodiscard]] PerformanceMetrics &defaultPerformanceMetrics() noexcept;

/** Starts a scope on the process-global default recorder. */
[[nodiscard]] PerformanceMetrics::Scope enterBlock(std::string_view name) noexcept;

} // namespace opk::perf

/** Internal helper: performs the final token-paste operation for generated scope variable names. */
#define OPK_PERF_METRICS_CONCAT_INNER_(a, b) a##b

/** Internal helper: expands macro arguments before token-pasting them into a variable name. */
#define OPK_PERF_METRICS_CONCAT_(a, b) OPK_PERF_METRICS_CONCAT_INNER_(a, b)

#ifdef __COUNTER__
/** Internal helper: creates a unique local variable name for RAII scope objects. */
#define OPK_PERF_METRICS_UNIQUE_NAME_(base) OPK_PERF_METRICS_CONCAT_(base, __COUNTER__)
#else
/** Internal helper: creates a unique local variable name for RAII scope objects. */
#define OPK_PERF_METRICS_UNIQUE_NAME_(base) OPK_PERF_METRICS_CONCAT_(base, __LINE__)
#endif

/** Drop-in global performance scope used by OPK instrumentation sites. */
#define OPK_PERF_SCOPE(name)                                                                       \
    [[maybe_unused]] auto OPK_PERF_METRICS_UNIQUE_NAME_(_opk_perf_scope_) =                        \
        ::opk::perf::enterBlock(name)

/** Returns a snapshot from the lazy process-global PerformanceMetrics recorder. */
#define OPK_PERF_SNAPSHOT() ::opk::perf::defaultPerformanceMetrics().snapshot()

/**
 * Enables or disables historical scope capture on the lazy process-global recorder.
 *
 * Aggregate metrics remain enabled.
 */
#define OPK_PERF_HISTORY_ENABLE(enabled)                                                           \
    ::opk::perf::defaultPerformanceMetrics().setHistoryEnabled(enabled)
