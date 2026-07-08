/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

namespace pek::perf {

namespace detail {
struct PerformanceMetricsState;
}

/**
 * Lightweight hierarchical performance metrics recorder.
 *
 * PerformanceMetrics is intentionally separate from PerformanceTracer. It
 * records nested span lifetimes through RAII Scope objects. The normal
 * enter/exit path uses a thread-local stack per PerformanceMetrics instance, so
 * nested measurements on different threads do not contend with each other after
 * the thread has acquired a slot.
 *
 * Scope objects must be destroyed on the same thread where they were created.
 * aggregateSnapshot() can be called while measured work is active. snapshot()
 * also includes trace spans; use it after the measured work has stopped when
 * trace data is enabled.
 */
class PerformanceMetrics {
  public:
    static constexpr std::uint64_t InvalidSpanId = std::numeric_limits<std::uint64_t>::max();
    static constexpr std::size_t MaxSpanNameLength = 64;

    // Optional exact trace entry for one scope lifetime.
    struct SpanRecord {
        std::uint64_t id = InvalidSpanId;
        std::uint64_t parentId = InvalidSpanId;
        std::array<char, MaxSpanNameLength + 1> name{};
        std::uint64_t startNs = 0;
        std::uint64_t endNs = 0;
        std::uint64_t threadId = 0;
        std::uint32_t depth = 0;
        bool nameTruncated = false;

        [[nodiscard]] std::string_view nameView() const noexcept {
            return name.data();
        }

        [[nodiscard]] bool complete() const noexcept {
            return endNs >= startNs && endNs != 0;
        }

        [[nodiscard]] std::uint64_t durationNs() const noexcept {
            return complete() ? endNs - startNs : 0;
        }
    };

    // Aggregated timing for one scope name at one nesting depth.
    struct MetricRecord {
        std::array<char, MaxSpanNameLength + 1> name{};
        std::uint32_t depth = 0;
        std::uint64_t count = 0;
        std::uint64_t totalNs = 0;
        std::uint64_t minNs = 0;
        std::uint64_t maxNs = 0;
        std::uint64_t lastNs = 0;
        bool nameTruncated = false;
        bool hasChildren = false;

        [[nodiscard]] std::string_view nameView() const noexcept {
            return name.data();
        }
    };

    // Point-in-time copy safe to read outside the recorder.
    struct Snapshot {
        std::vector<MetricRecord> metrics;
        std::vector<SpanRecord> spans;
        std::uint32_t droppedMetrics = 0;
        std::uint32_t droppedSpans = 0;
        bool threadSlotOverflow = false;
    };

    // RAII token returned by scope(); destruction records the elapsed time.
    class Scope {
      public:
        Scope() noexcept = default;
        Scope(const Scope &) = delete;
        Scope &operator=(const Scope &) = delete;
        Scope(Scope &&other) noexcept;
        Scope &operator=(Scope &&other) noexcept;
        ~Scope();

        void close() noexcept;

        [[nodiscard]] bool active() const noexcept {
            return metrics != nullptr;
        }

      private:
        friend class PerformanceMetrics;

        Scope(PerformanceMetrics *metrics,
              std::uint32_t generation,
              std::uint32_t slotIndex,
              std::uint32_t spanIndex,
              std::array<char, MaxSpanNameLength + 1> name,
              std::uint64_t startNs,
              std::uint32_t depth,
              bool nameTruncated,
              bool traceRecorded) noexcept;

        PerformanceMetrics *metrics = nullptr;
        std::uint32_t generation = 0;
        std::uint32_t slotIndex = 0;
        std::uint32_t spanIndex = 0;
        std::array<char, MaxSpanNameLength + 1> name{};
        std::uint64_t startNs = 0;
        std::uint32_t depth = 0;
        bool nameTruncated = false;
        bool traceRecorded = false;
    };

    PerformanceMetrics();
    ~PerformanceMetrics();

    PerformanceMetrics(const PerformanceMetrics &) = delete;
    PerformanceMetrics &operator=(const PerformanceMetrics &) = delete;
    PerformanceMetrics(PerformanceMetrics &&) = delete;
    PerformanceMetrics &operator=(PerformanceMetrics &&) = delete;

    [[nodiscard]] Scope scope(std::string_view name) noexcept;

    void setEnabled(bool enabled) noexcept;
    [[nodiscard]] bool enabled() const noexcept;

    void setTraceEnabled(bool enabled) noexcept;
    [[nodiscard]] bool traceEnabled() const noexcept;

    [[nodiscard]] Snapshot aggregateSnapshot() const;
    [[nodiscard]] Snapshot snapshot() const;
    void reset();

  private:
    void exitBlock(std::uint32_t generation,
                   std::uint32_t slotIndex,
                   std::uint32_t spanIndex,
                   const std::array<char, MaxSpanNameLength + 1> &name,
                   std::uint64_t startNs,
                   std::uint32_t depth,
                   bool nameTruncated,
                   bool traceRecorded) noexcept;

    std::unique_ptr<detail::PerformanceMetricsState> state;
};

/**
 * Binds a PerformanceMetrics instance to the current thread until destruction.
 * Nested bindings restore the previous current recorder.
 */
class ScopedMetricsContext {
  public:
    explicit ScopedMetricsContext(PerformanceMetrics &metrics) noexcept;
    ~ScopedMetricsContext();

    ScopedMetricsContext(const ScopedMetricsContext &) = delete;
    ScopedMetricsContext &operator=(const ScopedMetricsContext &) = delete;
    ScopedMetricsContext(ScopedMetricsContext &&) = delete;
    ScopedMetricsContext &operator=(ScopedMetricsContext &&) = delete;

  private:
    PerformanceMetrics *previous = nullptr;
};

[[nodiscard]] PerformanceMetrics *currentPerformanceMetrics() noexcept;

/**
 * Lazy global metrics recorder for no-init experiments. Function-local static
 * initialization is thread-safe in C++11 and later.
 */
[[nodiscard]] PerformanceMetrics &defaultPerformanceMetrics() noexcept;

/**
 * Convenience free function for:
 *
 *     auto scope = pek::perf::enterBlock("block.name");
 */
[[nodiscard]] PerformanceMetrics::Scope enterBlock(std::string_view name) noexcept;

/**
 * Convenience free function for the current thread-bound metrics recorder.
 * Returns an inactive Scope when no ScopedMetricsContext is bound.
 */
[[nodiscard]] PerformanceMetrics::Scope enterCurrentBlock(std::string_view name) noexcept;

} // namespace pek::perf

// Internal helper: performs the final token-paste operation for generated
// scope variable names.
#define PEK_PERF_METRICS_CONCAT_INNER_(a, b) a##b

// Internal helper: forces macro arguments such as __LINE__ or __COUNTER__ to
// expand before token-pasting them into a variable name.
#define PEK_PERF_METRICS_CONCAT_(a, b) PEK_PERF_METRICS_CONCAT_INNER_(a, b)

#ifdef __COUNTER__
// Internal helper: creates a unique local variable name for RAII scope objects.
#define PEK_PERF_METRICS_UNIQUE_NAME_(base) PEK_PERF_METRICS_CONCAT_(base, __COUNTER__)
#else
// Internal helper: creates a unique local variable name for RAII scope objects.
#define PEK_PERF_METRICS_UNIQUE_NAME_(base) PEK_PERF_METRICS_CONCAT_(base, __LINE__)
#endif

/**
 * Usage:
 *
 *     pek::perf::PerformanceMetrics metrics;
 *     {
 *         PEK_METRICS_SCOPE(metrics, "opchain");
 *         {
 *             PEK_METRICS_SCOPE(metrics, "inference");
 *         }
 *     }
 *
 *     const auto snapshot = metrics.snapshot();
 *
 * Thread-local current recorder usage:
 *
 *     {
 *         pek::perf::ScopedMetricsContext bind(metrics);
 *         PEK_METRICS_SCOPE_CURRENT("inference");
 *     }
 *
 * No-init global usage:
 *
 *     {
 *         PEK_METRICS_SCOPE_GLOBAL("startup");
 *     }
 *
 * Drop-in global usage:
 *
 *     PEK_PERF_RESET();
 *     {
 *         PEK_PERF_SCOPE("inference");
 *     }
 *     const auto snapshot = PEK_PERF_SNAPSHOT();
 *
 * Exact span traces are off by default. Enable them only around runs that need
 * per-scope span export:
 *
 *     PEK_PERF_TRACE_ENABLE(true);
 */
// Records one RAII scope into an explicitly owned PerformanceMetrics instance.
#define PEK_METRICS_SCOPE(metrics, name)                                                           \
    [[maybe_unused]] auto PEK_PERF_METRICS_UNIQUE_NAME_(_pek_metrics_scope_) = (metrics).scope(name)

// Records one RAII scope into the lazy process-global PerformanceMetrics
// instance. Use this when no explicit metrics object is passed around.
#define PEK_METRICS_SCOPE_GLOBAL(name)                                                             \
    [[maybe_unused]] auto PEK_PERF_METRICS_UNIQUE_NAME_(_pek_metrics_global_scope_) =              \
        ::pek::perf::enterBlock(name)

// Records one RAII scope into the current thread-bound PerformanceMetrics
// instance. The scope is inactive when no ScopedMetricsContext is bound.
#define PEK_METRICS_SCOPE_CURRENT(name)                                                            \
    [[maybe_unused]] auto PEK_PERF_METRICS_UNIQUE_NAME_(_pek_metrics_current_scope_) =             \
        ::pek::perf::enterCurrentBlock(name)

// Drop-in global performance scope used by PEK instrumentation sites.
#define PEK_PERF_SCOPE(name)                                                                       \
    [[maybe_unused]] auto PEK_PERF_METRICS_UNIQUE_NAME_(_pek_perf_scope_) =                        \
        ::pek::perf::enterBlock(name)

// Clears the lazy process-global PerformanceMetrics recorder.
#define PEK_PERF_RESET() ::pek::perf::defaultPerformanceMetrics().reset()

// Returns a snapshot from the lazy process-global PerformanceMetrics recorder.
#define PEK_PERF_SNAPSHOT() ::pek::perf::defaultPerformanceMetrics().snapshot()

// Enables or disables exact span trace collection on the lazy process-global
// recorder. Aggregate metrics are still recorded while the recorder is enabled.
#define PEK_PERF_TRACE_ENABLE(enabled)                                                             \
    ::pek::perf::defaultPerformanceMetrics().setTraceEnabled(enabled)
