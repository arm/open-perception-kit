/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "perf/PerformanceTracer.h"
#include "Log.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <thread>
#include <utility>

namespace pek::perf {

// ============================================================================
// PerformanceTracer Implementation
// ============================================================================

PerformanceTracer::PerformanceTracer() = default;

PerformanceTracer::~PerformanceTracer() = default;

void PerformanceTracer::start(const std::string &key) {
    auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(active_mutex_);
    active_timers_[key] = now;
}

std::chrono::nanoseconds PerformanceTracer::end(const std::string &key) {
    auto now = std::chrono::steady_clock::now();

    std::chrono::steady_clock::time_point start_time;
    {
        std::lock_guard<std::mutex> lock(active_mutex_);
        auto it = active_timers_.find(key);
        if (it == active_timers_.end()) {
            return std::chrono::nanoseconds(0); // Key not found
        }
        start_time = it->second;
        active_timers_.erase(it);
    }

    // Create measurement
    TimingMeasurement m;
    m.key = key;
    m.start_time = start_time;
    m.end_time = now;
    m.duration = std::chrono::duration_cast<std::chrono::nanoseconds>(now - start_time);

    // Add to current cycle
    {
        std::lock_guard<std::mutex> lock(current_cycle_mutex_);
        if (current_cycle_consumer_count_ > 0 || has_cycle_end_callbacks_.load()) {
            current_cycle_measurements_.push_back(m);
        }
    }

    // Add to history
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        measurement_history_[key].push_back(m);

        // Limit history size if configured
        if (max_measurements_per_key_ > 0) {
            auto &hist = measurement_history_[key];
            if (hist.size() > max_measurements_per_key_) {
                hist.erase(hist.begin(), hist.begin() + (hist.size() - max_measurements_per_key_));
            }
        }
    }

    return m.duration;
}

void PerformanceTracer::endCycle() {
    cycle_count_++;

    // Get current cycle measurements
    std::vector<TimingMeasurement> cycle_measurements;
    {
        std::lock_guard<std::mutex> lock(current_cycle_mutex_);
        cycle_measurements = current_cycle_measurements_;
        current_cycle_measurements_.clear();
    }

    // Calculate statistics if enabled
    if (auto_calculate_stats_) {
        calculateStats();
    }

    // Trigger callbacks
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        for (auto &callback : cycle_end_callbacks_) {
            callback(cycle_count_, cycle_measurements);
        }
    }

    // Cleanup old data
    cleanupOldMeasurements();
}

std::vector<TimingMeasurement> PerformanceTracer::getCurrentCycleMeasurements() const {
    std::lock_guard<std::mutex> lock(current_cycle_mutex_);
    return current_cycle_measurements_;
}

void PerformanceTracer::registerCurrentCycleConsumer() {
    std::lock_guard<std::mutex> lock(current_cycle_mutex_);
    current_cycle_consumer_count_++;
}

void PerformanceTracer::unregisterCurrentCycleConsumer() {
    std::lock_guard<std::mutex> lock(current_cycle_mutex_);
    if (current_cycle_consumer_count_ > 0 && --current_cycle_consumer_count_ == 0) {
        current_cycle_measurements_.clear();
    }
}

TimingStats PerformanceTracer::getStats(const std::string &key) const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    const auto it = stats_cache_.find(key);
    if (it != stats_cache_.end()) {
        return it->second;
    }
    return {};
}

std::map<std::string, TimingStats> PerformanceTracer::getAllStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_cache_;
}

std::vector<TimingMeasurement> PerformanceTracer::getMeasurements(const std::string &key) const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    const auto it = measurement_history_.find(key);
    if (it != measurement_history_.end()) {
        return it->second;
    }
    return {};
}

void PerformanceTracer::registerCycleEndCallback(CycleEndCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    cycle_end_callbacks_.push_back(std::move(callback));
    has_cycle_end_callbacks_.store(true);
}

void PerformanceTracer::reset() {
    // Acquire all locks in consistent order to prevent deadlocks
    std::lock_guard<std::mutex> lock1(active_mutex_);
    std::lock_guard<std::mutex> lock2(current_cycle_mutex_);
    std::lock_guard<std::mutex> lock3(history_mutex_);
    std::lock_guard<std::mutex> lock4(stats_mutex_);
    std::lock_guard<std::mutex> lock5(callback_mutex_);

    active_timers_.clear();
    current_cycle_measurements_.clear();
    measurement_history_.clear();
    stats_cache_.clear();
    cycle_count_ = 0;
    // Note: callbacks are not cleared on reset - they persist
}

void PerformanceTracer::removeMetrics(const std::string &key) {
    // Acquire all locks in consistent order to prevent deadlocks
    std::lock_guard<std::mutex> lock1(active_mutex_);
    std::lock_guard<std::mutex> lock2(current_cycle_mutex_);
    std::lock_guard<std::mutex> lock3(history_mutex_);
    std::lock_guard<std::mutex> lock4(stats_mutex_);

    // Remove from active timers (exact match)
    active_timers_.erase(key);

    // Remove from current cycle measurements (exact match)
    current_cycle_measurements_.erase(
        std::remove_if(current_cycle_measurements_.begin(),
                       current_cycle_measurements_.end(),
                       [&key](const TimingMeasurement &m) { return m.key == key; }),
        current_cycle_measurements_.end());

    // Remove from history (exact match)
    measurement_history_.erase(key);

    // Remove from stats cache (exact match)
    stats_cache_.erase(key);

    // Force recalculation on next cycle
    auto_calculate_stats_ = true;
}

size_t PerformanceTracer::getActiveTimerCount() const {
    std::lock_guard<std::mutex> lock(active_mutex_);
    return active_timers_.size();
}

bool PerformanceTracer::isActive(const std::string &key) const {
    std::lock_guard<std::mutex> lock(active_mutex_);
    return active_timers_.find(key) != active_timers_.end();
}

void PerformanceTracer::calculateStats() {
    std::lock_guard<std::mutex> lock(history_mutex_);
    std::lock_guard<std::mutex> lock2(stats_mutex_);

    for (const auto &pair : measurement_history_) {
        const std::string &key = pair.first;
        const auto &measurements = pair.second;

        if (measurements.empty()) {
            continue;
        }

        TimingStats stats;
        stats.key = key;
        stats.count = measurements.size();

        // Collect durations for percentile calculation
        std::vector<std::chrono::nanoseconds> durations;
        durations.reserve(measurements.size());

        for (const auto &m : measurements) {
            stats.total += m.duration;
            stats.min = std::min(stats.min, m.duration);
            stats.max = std::max(stats.max, m.duration);
            durations.push_back(m.duration);
        }

        // Safe division - count is guaranteed > 0 here
        stats.avg = stats.total / static_cast<long long>(stats.count);

        // Calculate percentiles using proper index calculation
        std::sort(durations.begin(), durations.end());
        const size_t size = durations.size();
        const size_t p50_idx = (size * 50) / 100;
        const size_t p95_idx = (size * 95) / 100;
        const size_t p99_idx = (size * 99) / 100;

        // Clamp to valid indices
        if (p50_idx < size)
            stats.p50 = durations[p50_idx];
        if (p95_idx < size)
            stats.p95 = durations[p95_idx];
        if (p99_idx < size)
            stats.p99 = durations[p99_idx];

        stats_cache_[key] = stats;
    }
}

void PerformanceTracer::cleanupOldMeasurements() {
    // Cleanup is already handled in end() method with max_measurements_per_key_
    // This method is kept for future extensions if needed
}

std::string PerformanceTracer::toJSON() const {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"cycle\": " << cycle_count_ << ",\n";
    oss << "  \"stats\": [\n";

    auto stats = getAllStats();
    bool first = true;
    for (const auto &pair : stats) {
        if (!first)
            oss << ",\n";
        first = false;

        const auto &s = pair.second;
        oss << "    {\n";
        oss << "      \"key\": \"" << s.key << "\",\n";
        oss << "      \"count\": " << s.count << ",\n";
        oss << "      \"avg_ms\": " << std::fixed << std::setprecision(3) << s.avg_ms() << ",\n";
        oss << "      \"p50_ms\": " << s.p50_ms() << ",\n";
        oss << "      \"p95_ms\": " << s.p95_ms() << ",\n";
        oss << "      \"p99_ms\": " << s.p99_ms() << ",\n";
        oss << "      \"min_ms\": " << s.min_ms() << ",\n";
        oss << "      \"max_ms\": " << s.max_ms() << "\n";
        oss << "    }";
    }

    oss << "\n  ]\n";
    oss << "}\n";

    return oss.str();
}

void PerformanceTracer::printSummary() const {
    auto stats = getAllStats();

    std::ostringstream oss;
    oss << "\n╔═══════════════════════════════════════════════════════════════════════════╗\n";
    oss << "║                     Performance Tracer Summary                            ║\n";
    oss << "╠═══════════════════════════════════════════════════════════════════════════╣\n";
    oss << "║ Cycle: " << std::setw(4) << cycle_count_ << " | Active Timers: " << std::setw(3)
        << getActiveTimerCount() << " | Total Keys: " << std::setw(3) << stats.size()
        << "                        ║\n";
    oss << "╠═══════════════════════════════════════════════════════════════════════════╣\n";

    if (stats.empty()) {
        oss << "║                          No measurements yet                              ║\n";
    } else {
        oss << "║ Key                  │ Count │  Avg(ms) │  P50(ms) │  P95(ms) │  P99(ms) ║\n";
        oss << "╠══════════════════════╪═══════╪══════════╪══════════╪══════════╪══════════╣\n";

        for (const auto &pair : stats) {
            const auto &s = pair.second;
            oss << "║ " << std::left << std::setw(20) << s.key.substr(0, 20) << " │ " << std::right
                << std::setw(5) << s.count << " │ " << std::setw(8) << std::fixed
                << std::setprecision(2) << s.avg_ms() << " │ " << std::setw(8) << std::fixed
                << std::setprecision(2) << s.p50_ms() << " │ " << std::setw(8) << std::fixed
                << std::setprecision(2) << s.p95_ms() << " │ " << std::setw(8) << std::fixed
                << std::setprecision(2) << s.p99_ms() << " ║\n";
        }
    }

    oss << "╚═══════════════════════════════════════════════════════════════════════════╝\n";
    pek::log::info("{}", oss.str());
}

// ============================================================================
// PerformanceMonitor Implementation
// ============================================================================

PerformanceMonitor::PerformanceMonitor(PerformanceTracer *tracer) : tracer_(tracer) {}

PerformanceMonitor::~PerformanceMonitor() = default;

void PerformanceMonitor::print() const {
    pek::log::info("{}\n", format());
}

void PerformanceMonitor::printCycle(size_t cycle_number) const {
    pek::log::info("\n=== Cycle {} ===\n", cycle_number);
    print();
}

void PerformanceMonitor::startLiveMonitoring() {
    pek::log::info("Starting live monitoring (Ctrl+C to stop)...\n\n");

    while (true) {
        clearScreen();
        pek::log::info("{}\n", format());
        std::this_thread::sleep_for(refresh_interval_);
    }
}

std::string PerformanceMonitor::format() const {
    if (!tracer_) {
        return "Error: No tracer instance";
    }

    switch (display_mode_) {
    case DisplayMode::COMPACT:
    case DisplayMode::LIVE_UPDATE:
        return formatCompact();
    case DisplayMode::DETAILED:
        return formatDetailed();
    }
    return "";
}

std::string PerformanceMonitor::formatKey(const std::string &key) const {
    if (!tracer_) {
        return "Error: No tracer instance";
    }

    const auto stats = tracer_->getStats(key);
    if (stats.count == 0) {
        return "No data for key: " + key;
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << key << ": "
        << "avg=" << stats.avg_ms() << "ms, "
        << "p50=" << stats.p50_ms() << "ms, "
        << "p95=" << stats.p95_ms() << "ms, "
        << "count=" << stats.count;

    return oss.str();
}

std::string PerformanceMonitor::formatCompact() const {
    std::ostringstream oss;

    oss << "+- Performance Monitor (Cycle " << tracer_->getCycleNumber() << ") -+\n";

    auto all_stats = tracer_->getAllStats();

    // Filter by monitored keys if specified
    std::vector<std::string> keys_to_show;
    if (monitored_keys_.empty()) {
        for (const auto &pair : all_stats) {
            keys_to_show.push_back(pair.first);
        }
    } else {
        keys_to_show = monitored_keys_;
    }

    for (const auto &key : keys_to_show) {
        auto it = all_stats.find(key);
        if (it != all_stats.end()) {
            const auto &s = it->second;
            oss << "| " << std::left << std::setw(20) << key.substr(0, 20) << " avg:" << std::right
                << std::setw(7) << std::fixed << std::setprecision(2) << s.avg_ms() << "ms"
                << " p95:" << std::setw(7) << s.p95_ms() << "ms"
                << " n=" << std::setw(4) << s.count << " |\n";
        }
    }

    oss << "+" << std::string(50, '-') << "+";

    return oss.str();
}

std::string PerformanceMonitor::formatDetailed() const {
    std::ostringstream oss;

    oss << "\n╔════════════════════════════════════════════════════════════════════════════╗\n";
    oss << "║                    Performance Monitor - Detailed View                     ║\n";
    oss << "╠════════════════════════════════════════════════════════════════════════════╣\n";
    oss << "║ Cycle: " << std::setw(6) << tracer_->getCycleNumber() << " │ Active: " << std::setw(3)
        << tracer_->getActiveTimerCount() << "                                              ║\n";
    oss << "╠════════════════════════════════════════════════════════════════════════════╣\n";

    auto all_stats = tracer_->getAllStats();

    if (all_stats.empty()) {
        oss << "║                           No measurements recorded                         ║\n";
    } else {
        oss << "║ Key                │   Count │ Avg(ms) │ P50(ms) │ P95(ms) │ P99(ms) │ Max  ║\n";
        oss << "╠════════════════════╪═════════╪═════════╪═════════╪═════════╪═════════╪══════╣\n";

        for (const auto &pair : all_stats) {
            const auto &s = pair.second;

            // Filter if monitored keys specified
            if (!monitored_keys_.empty()) {
                if (std::find(monitored_keys_.begin(), monitored_keys_.end(), s.key) ==
                    monitored_keys_.end()) {
                    continue;
                }
            }

            oss << "║ " << std::left << std::setw(18) << s.key.substr(0, 18) << " │ " << std::right
                << std::setw(7) << s.count << " │ " << std::setw(7) << std::fixed
                << std::setprecision(2) << s.avg_ms() << " │ " << std::setw(7) << std::fixed
                << std::setprecision(2) << s.p50_ms() << " │ " << std::setw(7) << std::fixed
                << std::setprecision(2) << s.p95_ms() << " │ " << std::setw(7) << std::fixed
                << std::setprecision(2) << s.p99_ms() << " │ " << std::setw(4) << std::fixed
                << std::setprecision(2) << s.max_ms() << " ║\n";
        }
    }

    oss << "╚════════════════════════════════════════════════════════════════════════════╝";

    return oss.str();
}

void PerformanceMonitor::clearScreen() const {
    // ANSI escape code to clear screen and move cursor to top
    pek::log::info("\033[2J\033[H");
    pek::log::flush();
}

// ============================================================================
// Global Instance
// ============================================================================

PerformanceTracer *getGlobalTracer() {
    static PerformanceTracer global_tracer;
    return &global_tracer;
}

} // namespace pek::perf
