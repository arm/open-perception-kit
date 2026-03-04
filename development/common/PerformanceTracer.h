/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

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
     * Remove all metrics for a specific key
     * @param key The key to remove metrics for
     */
    void removeMetrics(const std::string &key);

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

} // namespace amp
