#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace amp {

// ============================================================================
// Core Data Structures
// ============================================================================

struct TimingMeasurement {
    std::string key;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    std::chrono::nanoseconds duration;

    TimingMeasurement() : duration(0) {}

    double duration_ms() const {
        return std::chrono::duration<double, std::milli>(duration).count();
    }

    double duration_us() const {
        return std::chrono::duration<double, std::micro>(duration).count();
    }

    double duration_s() const {
        return std::chrono::duration<double>(duration).count();
    }
};

struct TimingStats {
    std::string key;
    size_t count;
    std::chrono::nanoseconds total;
    std::chrono::nanoseconds min;
    std::chrono::nanoseconds max;
    std::chrono::nanoseconds avg;
    std::chrono::nanoseconds p50;
    std::chrono::nanoseconds p95;
    std::chrono::nanoseconds p99;

    TimingStats()
        : count(0), total(0), min(std::chrono::nanoseconds::max()), max(0), avg(0), p50(0), p95(0),
          p99(0) {}

    double avg_ms() const {
        return std::chrono::duration<double, std::milli>(avg).count();
    }

    double p50_ms() const {
        return std::chrono::duration<double, std::milli>(p50).count();
    }

    double p95_ms() const {
        return std::chrono::duration<double, std::milli>(p95).count();
    }

    double p99_ms() const {
        return std::chrono::duration<double, std::milli>(p99).count();
    }

    double min_ms() const {
        return std::chrono::duration<double, std::milli>(min).count();
    }

    double max_ms() const {
        return std::chrono::duration<double, std::milli>(max).count();
    }
};

// ============================================================================
// Performance Tracer - Thread-safe key-value timing store
// ============================================================================

class PerformanceTracer {
  public:
    PerformanceTracer();
    ~PerformanceTracer();

    // ---- Core Timing API ----

    /**
     * Start timing measurement for a key
     * @param key Unique identifier for this measurement
     */
    void start(const std::string &key);

    /**
     * End timing measurement for a key
     * @param key Unique identifier that was used in start()
     * @return Duration in nanoseconds, or 0 if key not found
     */
    std::chrono::nanoseconds end(const std::string &key);

    /**
     * Scoped timing helper - automatically calls end() on destruction
     */
    class ScopedTimer {
      public:
        ScopedTimer(PerformanceTracer *tracer, const std::string &key)
            : tracer_(tracer), key_(key) {
            if (tracer_)
                tracer_->start(key_);
        }
        ~ScopedTimer() {
            if (tracer_)
                tracer_->end(key_);
        }

      private:
        PerformanceTracer *tracer_;
        std::string key_;
    };

    // ---- Cycle Management ----

    /**
     * Mark the end of a measurement cycle
     * Triggers statistics calculation and optional callbacks
     */
    void endCycle();

    /**
     * Get current cycle number
     */
    size_t getCycleNumber() const {
        return cycle_count_;
    }

    // ---- Data Access ----

    /**
     * Get all measurements from current cycle
     */
    std::vector<TimingMeasurement> getCurrentCycleMeasurements() const;

    /**
     * Get statistics for a specific key across all cycles
     */
    TimingStats getStats(const std::string &key) const;

    /**
     * Get statistics for all keys
     */
    std::map<std::string, TimingStats> getAllStats() const;

    /**
     * Get measurements for current cycle by key
     */
    std::vector<TimingMeasurement> getMeasurements(const std::string &key) const;

    // ---- Configuration ----

    /**
     * Enable/disable automatic statistics calculation on endCycle()
     */
    void setAutoCalculateStats(bool enable) {
        auto_calculate_stats_ = enable;
    }

    /**
     * Set maximum number of measurements to keep per key (0 = unlimited)
     */
    void setMaxMeasurementsPerKey(size_t max) {
        max_measurements_per_key_ = max;
    }

    /**
     * Register callback to be called at end of each cycle
     * Callback receives: cycle_number, all measurements from that cycle
     */
    using CycleEndCallback = std::function<void(size_t, const std::vector<TimingMeasurement> &)>;
    void registerCycleEndCallback(CycleEndCallback callback);

    /**
     * Clear all accumulated data
     */
    void reset();

    /**
     * Get total number of active timers (start called, end not yet called)
     */
    size_t getActiveTimerCount() const;

    // ---- Utility Methods ----

    /**
     * Check if a key is currently being timed
     */
    bool isActive(const std::string &key) const;

    /**
     * Export all data to JSON string
     */
    std::string toJSON() const;

    /**
     * Print summary to stdout
     */
    void printSummary() const;

  private:
    void calculateStats();
    void cleanupOldMeasurements();

    // Active timers (key -> start_time_point)
    mutable std::mutex active_mutex_;
    std::map<std::string, std::chrono::steady_clock::time_point> active_timers_;

    // Current cycle measurements
    mutable std::mutex current_cycle_mutex_;
    std::vector<TimingMeasurement> current_cycle_measurements_;

    // Historical data (key -> list of measurements)
    mutable std::mutex history_mutex_;
    std::map<std::string, std::vector<TimingMeasurement>> measurement_history_;

    // Statistics (key -> stats)
    mutable std::mutex stats_mutex_;
    std::map<std::string, TimingStats> stats_cache_;

    // Configuration
    size_t cycle_count_;
    bool auto_calculate_stats_;
    size_t max_measurements_per_key_;

    // Callbacks
    mutable std::mutex callback_mutex_;
    std::vector<CycleEndCallback> cycle_end_callbacks_;
};

// ============================================================================
// Monitor - Real-time display of tracer data
// ============================================================================

class PerformanceMonitor {
  public:
    enum class DisplayMode {
        COMPACT,    // One line per key
        DETAILED,   // Full statistics
        LIVE_UPDATE // Continuous refresh
    };

    PerformanceMonitor(PerformanceTracer *tracer);
    ~PerformanceMonitor();

    /**
     * Set display mode
     */
    void setDisplayMode(DisplayMode mode) {
        display_mode_ = mode;
    }

    /**
     * Set keys to monitor (empty = monitor all)
     */
    void setKeysToMonitor(const std::vector<std::string> &keys) {
        monitored_keys_ = keys;
    }

    /**
     * Set refresh interval for live updates
     */
    void setRefreshInterval(std::chrono::milliseconds interval) {
        refresh_interval_ = interval;
    }

    /**
     * Print current statistics to stdout
     */
    void print() const;

    /**
     * Print statistics for specific cycle
     */
    void printCycle(size_t cycle_number) const;

    /**
     * Start continuous live monitoring (blocking call)
     * Press Ctrl+C to stop
     */
    void startLiveMonitoring();

    /**
     * Format statistics as string
     */
    std::string format() const;

    /**
     * Format specific key statistics
     */
    std::string formatKey(const std::string &key) const;

  private:
    PerformanceTracer *tracer_;
    DisplayMode display_mode_;
    std::vector<std::string> monitored_keys_;
    std::chrono::milliseconds refresh_interval_;

    std::string formatCompact() const;
    std::string formatDetailed() const;
    void clearScreen() const;
};

// ============================================================================
// Global Instance (optional convenience)
// ============================================================================

/**
 * Get global tracer instance (lazy initialized)
 */
PerformanceTracer *getGlobalTracer();

/**
 * Convenience macros for global tracer
 */
#define AMP_TRACE_START(key) amp::getGlobalTracer()->start(key)
#define AMP_TRACE_END(key) amp::getGlobalTracer()->end(key)
#define AMP_TRACE_SCOPE(key)                                                                       \
    amp::PerformanceTracer::ScopedTimer _amp_timer_##__LINE__(amp::getGlobalTracer(), key)
#define AMP_TRACE_END_CYCLE() amp::getGlobalTracer()->endCycle()

// ============================================================================
// Fast Performance Tracer - Zero-overhead timing for hot paths
// ============================================================================

namespace fast {

// Predefined timing keys for common operations
enum class TimingKey : uint32_t {
    PREPROCESSING = 0,
    INFERENCE = 1,
    POSTPROCESSING = 2,
    FRAME_TOTAL = 3,
    CUSTOM_0 = 4,
    CUSTOM_1 = 5,
    CUSTOM_2 = 6,
    CUSTOM_3 = 7,
    MAX_KEYS = 8
};

// Lock-free statistics structure (cache-line aligned)
struct alignas(64) FastStats {
    std::atomic<uint64_t> count{0};
    std::atomic<uint64_t> total_ns{0};
    std::atomic<uint64_t> min_ns{UINT64_MAX};
    std::atomic<uint64_t> max_ns{0};

    // For percentiles, we need to collect samples (lock-free ring buffer)
    static constexpr size_t SAMPLE_SIZE = 256;
    std::array<std::atomic<uint64_t>, SAMPLE_SIZE> samples;
    std::atomic<size_t> sample_index{0};

    FastStats() {
        for (auto &s : samples) {
            s.store(0, std::memory_order_relaxed);
        }
    }

    // Update statistics (lock-free)
    inline void update(uint64_t duration_ns) {
        count.fetch_add(1, std::memory_order_relaxed);
        total_ns.fetch_add(duration_ns, std::memory_order_relaxed);

        // Update min
        uint64_t current_min = min_ns.load(std::memory_order_relaxed);
        while (duration_ns < current_min &&
               !min_ns.compare_exchange_weak(current_min, duration_ns, std::memory_order_relaxed))
            ;

        // Update max
        uint64_t current_max = max_ns.load(std::memory_order_relaxed);
        while (duration_ns > current_max &&
               !max_ns.compare_exchange_weak(current_max, duration_ns, std::memory_order_relaxed))
            ;

        // Store sample for percentile calculation
        size_t idx = sample_index.fetch_add(1, std::memory_order_relaxed) % SAMPLE_SIZE;
        samples[idx].store(duration_ns, std::memory_order_relaxed);
    }

    // Get average in nanoseconds
    inline uint64_t avg_ns() const {
        uint64_t c = count.load(std::memory_order_relaxed);
        return c > 0 ? total_ns.load(std::memory_order_relaxed) / c : 0;
    }

    // Get average in milliseconds
    inline double avg_ms() const {
        return avg_ns() / 1e6;
    }

    // Calculate percentile (requires copying samples, not real-time)
    double percentile_ms(double p) const;
};

// Accessor functions for thread-local and static data (Meyer's singleton pattern)
// This avoids static initialization order fiasco

inline std::array<std::chrono::steady_clock::time_point, static_cast<size_t>(TimingKey::MAX_KEYS)> &
getActiveTimers() {
    thread_local std::array<std::chrono::steady_clock::time_point,
                            static_cast<size_t>(TimingKey::MAX_KEYS)>
        timers;
    return timers;
}

inline std::array<FastStats, static_cast<size_t>(TimingKey::MAX_KEYS)> &getStats() {
    static std::array<FastStats, static_cast<size_t>(TimingKey::MAX_KEYS)>
        stats{}; // Zero-initialize
    static bool initialized = []() {
        // Ensure all atomic samples are properly zero-initialized
        for (auto &stat : stats) {
            for (auto &sample : stat.samples) {
                sample.store(0, std::memory_order_relaxed);
            }
        }
        return true;
    }();
    (void)initialized; // Suppress unused warning
    return stats;
}

inline const char *getKeyName(TimingKey key) {
    static const char *names[static_cast<size_t>(TimingKey::MAX_KEYS)] = {"preprocessing",
                                                                          "inference",
                                                                          "postprocessing",
                                                                          "frame_total",
                                                                          "custom_0",
                                                                          "custom_1",
                                                                          "custom_2",
                                                                          "custom_3"};
    return names[static_cast<size_t>(key)];
}

/**
 * Start timing (inlined, zero overhead)
 */
inline void start(TimingKey key) __attribute__((always_inline));
inline void start(TimingKey key) {
    getActiveTimers()[static_cast<size_t>(key)] = std::chrono::steady_clock::now();
}

/**
 * End timing and update statistics (inlined, lock-free)
 */
inline std::chrono::nanoseconds end(TimingKey key) __attribute__((always_inline));
inline std::chrono::nanoseconds end(TimingKey key) {
    auto end_time = std::chrono::steady_clock::now();
    auto &start_time = getActiveTimers()[static_cast<size_t>(key)];
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);

    getStats()[static_cast<size_t>(key)].update(duration.count());

    return duration;
}

/**
 * Fast scoped timer - zero overhead RAII
 */
template <TimingKey Key> class ScopedTimer {
    std::chrono::steady_clock::time_point start_;

  public:
    inline ScopedTimer() __attribute__((always_inline))
    : start_(std::chrono::steady_clock::now()) {}

    inline ~ScopedTimer() __attribute__((always_inline)) {
        auto duration = std::chrono::steady_clock::now() - start_;
        getStats()[static_cast<size_t>(Key)].update(
            std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
    }
};

/**
 * Get statistics for a key
 */
inline const FastStats &getStatsForKey(TimingKey key) {
    return getStats()[static_cast<size_t>(key)];
}

/**
 * Reset all statistics
 */
void reset();

/**
 * Print statistics summary
 */
void printSummary();

/**
 * Export to JSON
 */
std::string toJSON();

} // namespace fast

/**
 * Fast trace convenience macros (compile-time resolved)
 */
#define AMP_FAST_START(key) amp::fast::start(amp::fast::TimingKey::key)
#define AMP_FAST_END(key) amp::fast::end(amp::fast::TimingKey::key)
#define AMP_FAST_SCOPE(key)                                                                        \
    amp::fast::ScopedTimer<amp::fast::TimingKey::key> _amp_fast_timer_##__LINE__

} // namespace amp
