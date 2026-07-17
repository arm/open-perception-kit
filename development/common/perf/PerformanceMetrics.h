/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pek::perf {

namespace detail {
struct PerformanceMetricsState;
using PerformanceMetricsStatePtr = std::unique_ptr<PerformanceMetricsState>;
} // namespace detail

/**
 * Hierarchical block timer for PEK instrumentation.
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
    static constexpr std::size_t MaxSpanNameLength = 64;

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
        std::array<char, MaxSpanNameLength + 1> name{};

        /** Monotonic start timestamp in nanoseconds. */
        std::uint64_t startNs = 0;

        /** Monotonic end timestamp in nanoseconds. */
        std::uint64_t endNs = 0;

        /** Hashed std::thread::id of the thread that recorded this span. */
        std::uint64_t threadId = 0;

        /** Nesting depth at which this span was opened. */
        std::uint32_t depth = 0;

        /** True when the original scope name exceeded MaxSpanNameLength. */
        bool nameTruncated = false;

        /** Returns the copied scope name as a string_view. */
        [[nodiscard]] std::string_view nameView() const noexcept {
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
        std::array<char, MaxSpanNameLength + 1> name{};

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
        [[nodiscard]] std::string_view nameView() const noexcept {
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

    PerformanceMetrics();

    /**
     * Destroys the recorder.
     *
     * If an automatic CSV export path is configured, completed historical spans
     * are written best-effort during destruction.
     */
    ~PerformanceMetrics();

    /** Recorder instances own thread-local slot state and cannot be copied. */
    PerformanceMetrics(const PerformanceMetrics &) = delete;

    /** Recorder instances own thread-local slot state and cannot be copied. */
    PerformanceMetrics &operator=(const PerformanceMetrics &) = delete;

    /** Recorder instances are address-stable for thread-local frame lookup. */
    PerformanceMetrics(PerformanceMetrics &&) = delete;

    /** Recorder instances are address-stable for thread-local frame lookup. */
    PerformanceMetrics &operator=(PerformanceMetrics &&) = delete;

    /**
     * Starts measuring a named scope.
     *
     * The name is copied immediately into fixed-size storage. Returns an
     * inactive Scope when the recorder is disabled or capacity is exhausted.
     */
    [[nodiscard]] Scope scope(std::string_view name) noexcept;

    /** Enables or disables aggregate and historical recording. */
    void setEnabled(bool enabled) noexcept;

    /** Returns whether aggregate and historical recording are enabled. */
    [[nodiscard]] bool enabled() const noexcept;

    /** Enables or disables storing completed spans for snapshots and CSV export. */
    void setHistoryEnabled(bool enabled) noexcept;

    /** Returns whether completed span history is currently recorded. */
    [[nodiscard]] bool historyEnabled() const noexcept;

    /** Compatibility alias for setHistoryEnabled(). */
    void setTraceEnabled(bool enabled) noexcept;

    /** Compatibility alias for historyEnabled(). */
    [[nodiscard]] bool traceEnabled() const noexcept;

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
    static detail::PerformanceMetricsStatePtr createState();

    void exitBlock(std::uint32_t slotIndex,
                   std::uint32_t metricIndex,
                   std::uint64_t spanId,
                   std::uint64_t parentSpanId,
                   std::uint64_t startNs,
                   std::uint32_t depth,
                   bool historyRecorded) noexcept;

    detail::PerformanceMetricsStatePtr state;
};

/**
 * Binds a PerformanceMetrics instance to the current thread until destruction.
 * Nested bindings restore the previous current recorder.
 */
class ScopedMetricsContext {
  public:
    /** Binds the supplied recorder as the current recorder for this thread. */
    explicit ScopedMetricsContext(PerformanceMetrics &metrics) noexcept;

    /** Restores the previously bound current recorder for this thread. */
    ~ScopedMetricsContext();

    /** Context bindings are scoped and cannot be copied. */
    ScopedMetricsContext(const ScopedMetricsContext &) = delete;

    /** Context bindings are scoped and cannot be copied. */
    ScopedMetricsContext &operator=(const ScopedMetricsContext &) = delete;

    /** Context bindings are scoped and cannot be moved. */
    ScopedMetricsContext(ScopedMetricsContext &&) = delete;

    /** Context bindings are scoped and cannot be moved. */
    ScopedMetricsContext &operator=(ScopedMetricsContext &&) = delete;

  private:
    PerformanceMetrics *previous = nullptr;
};

/** Returns the PerformanceMetrics instance bound to the current thread, if any. */
[[nodiscard]] PerformanceMetrics *currentPerformanceMetrics() noexcept;

/**
 * Lazy process-wide metrics recorder for no-init experiments. The recorder is
 * destroyed normally at process shutdown, including best-effort automatic CSV
 * export when configured, without leaking the process-wide recorder.
 */
[[nodiscard]] PerformanceMetrics &defaultPerformanceMetrics() noexcept;

/** Starts a scope on the process-global default recorder. */
[[nodiscard]] PerformanceMetrics::Scope enterBlock(std::string_view name) noexcept;

/** Starts a scope on the current thread-bound recorder, or returns an inactive scope. */
[[nodiscard]] PerformanceMetrics::Scope enterCurrentBlock(std::string_view name) noexcept;

} // namespace pek::perf

/** Internal helper: performs the final token-paste operation for generated scope variable names. */
#define PEK_PERF_METRICS_CONCAT_INNER_(a, b) a##b

/** Internal helper: expands macro arguments before token-pasting them into a variable name. */
#define PEK_PERF_METRICS_CONCAT_(a, b) PEK_PERF_METRICS_CONCAT_INNER_(a, b)

#ifdef __COUNTER__
/** Internal helper: creates a unique local variable name for RAII scope objects. */
#define PEK_PERF_METRICS_UNIQUE_NAME_(base) PEK_PERF_METRICS_CONCAT_(base, __COUNTER__)
#else
/** Internal helper: creates a unique local variable name for RAII scope objects. */
#define PEK_PERF_METRICS_UNIQUE_NAME_(base) PEK_PERF_METRICS_CONCAT_(base, __LINE__)
#endif

/** Records one RAII scope into an explicitly owned PerformanceMetrics instance. */
#define PEK_METRICS_SCOPE(metrics, name)                                                           \
    [[maybe_unused]] auto PEK_PERF_METRICS_UNIQUE_NAME_(_pek_metrics_scope_) = (metrics).scope(name)

/**
 * Records one RAII scope into the lazy process-global PerformanceMetrics instance.
 *
 * Use this when no explicit metrics object is passed around.
 */
#define PEK_METRICS_SCOPE_GLOBAL(name)                                                             \
    [[maybe_unused]] auto PEK_PERF_METRICS_UNIQUE_NAME_(_pek_metrics_global_scope_) =              \
        ::pek::perf::enterBlock(name)

/**
 * Records one RAII scope into the current thread-bound PerformanceMetrics instance.
 *
 * The scope is inactive when no ScopedMetricsContext is bound.
 */
#define PEK_METRICS_SCOPE_CURRENT(name)                                                            \
    [[maybe_unused]] auto PEK_PERF_METRICS_UNIQUE_NAME_(_pek_metrics_current_scope_) =             \
        ::pek::perf::enterCurrentBlock(name)

/** Drop-in global performance scope used by PEK instrumentation sites. */
#define PEK_PERF_SCOPE(name)                                                                       \
    [[maybe_unused]] auto PEK_PERF_METRICS_UNIQUE_NAME_(_pek_perf_scope_) =                        \
        ::pek::perf::enterBlock(name)

/** Returns a snapshot from the lazy process-global PerformanceMetrics recorder. */
#define PEK_PERF_SNAPSHOT() ::pek::perf::defaultPerformanceMetrics().snapshot()

/**
 * Enables or disables historical scope capture on the lazy process-global recorder.
 *
 * Aggregate metrics are still recorded while the recorder is enabled.
 */
#define PEK_PERF_HISTORY_ENABLE(enabled)                                                           \
    ::pek::perf::defaultPerformanceMetrics().setHistoryEnabled(enabled)

/** Compatibility alias for earlier trace naming. */
#define PEK_PERF_TRACE_ENABLE(enabled) PEK_PERF_HISTORY_ENABLE(enabled)
