#include "PerformanceTracer.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace amp {

// ============================================================================
// PerformanceTracer Implementation
// ============================================================================

PerformanceTracer::PerformanceTracer()
    : cycle_count_(0), auto_calculate_stats_(true), max_measurements_per_key_(1000) {}

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
        current_cycle_measurements_.push_back(m);
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
    cycle_end_callbacks_.push_back(callback);
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

    std::cout
        << "\n╔═══════════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                     Performance Tracer Summary                            ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════════════════════════╣\n";
    std::cout << "║ Cycle: " << std::setw(4) << cycle_count_ << " | Active Timers: " << std::setw(3)
              << getActiveTimerCount() << " | Total Keys: " << std::setw(3) << stats.size()
              << "                        ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════════════════════════╣\n";

    if (stats.empty()) {
        std::cout
            << "║                          No measurements yet                              ║\n";
    } else {
        std::cout
            << "║ Key                  │ Count │  Avg(ms) │  P50(ms) │  P95(ms) │  P99(ms) ║\n";
        std::cout
            << "╠══════════════════════╪═══════╪══════════╪══════════╪══════════╪══════════╣\n";

        for (const auto &pair : stats) {
            const auto &s = pair.second;
            std::cout << "║ " << std::left << std::setw(20) << s.key.substr(0, 20) << " │ "
                      << std::right << std::setw(5) << s.count << " │ " << std::setw(8)
                      << std::fixed << std::setprecision(2) << s.avg_ms() << " │ " << std::setw(8)
                      << std::fixed << std::setprecision(2) << s.p50_ms() << " │ " << std::setw(8)
                      << std::fixed << std::setprecision(2) << s.p95_ms() << " │ " << std::setw(8)
                      << std::fixed << std::setprecision(2) << s.p99_ms() << " ║\n";
        }
    }

    std::cout << "╚═══════════════════════════════════════════════════════════════════════════╝\n";
}

// ============================================================================
// PerformanceMonitor Implementation
// ============================================================================

PerformanceMonitor::PerformanceMonitor(PerformanceTracer *tracer)
    : tracer_(tracer), display_mode_(DisplayMode::DETAILED),
      refresh_interval_(std::chrono::milliseconds(1000)) {}

PerformanceMonitor::~PerformanceMonitor() = default;

void PerformanceMonitor::print() const {
    std::cout << format() << std::endl;
}

void PerformanceMonitor::printCycle(size_t cycle_number) const {
    std::cout << "\n=== Cycle " << cycle_number << " ===" << std::endl;
    print();
}

void PerformanceMonitor::startLiveMonitoring() {
    std::cout << "Starting live monitoring (Ctrl+C to stop)...\n" << std::endl;

    while (true) {
        clearScreen();
        std::cout << format() << std::endl;
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
    std::cout << "\033[2J\033[H" << std::flush;
}

// ============================================================================
// Global Instance
// ============================================================================

static PerformanceTracer *g_global_tracer = nullptr;
static std::mutex g_global_mutex;

PerformanceTracer *getGlobalTracer() {
    std::lock_guard<std::mutex> lock(g_global_mutex);
    if (!g_global_tracer) {
        g_global_tracer = new PerformanceTracer();
    }
    return g_global_tracer;
}

// ============================================================================
// Fast Performance Tracer Implementation
// ============================================================================

namespace fast {

// All static data is now managed via inline accessor functions in the header
// to avoid static initialization order fiasco

double FastStats::percentile_ms(double p) const {
    // Collect samples
    std::vector<uint64_t> sorted_samples;
    sorted_samples.reserve(SAMPLE_SIZE);

    for (const auto &s : samples) {
        uint64_t val = s.load(std::memory_order_relaxed);
        if (val > 0) {
            sorted_samples.push_back(val);
        }
    }

    if (sorted_samples.empty()) {
        return 0.0;
    }

    std::sort(sorted_samples.begin(), sorted_samples.end());

    size_t idx = static_cast<size_t>((sorted_samples.size() - 1) * p);
    return sorted_samples[idx] / 1e6; // Convert ns to ms
}

// getStatsForKey and getKeyName are now inline functions in the header

void reset() {
    for (auto &s : getStats()) {
        s.count.store(0, std::memory_order_relaxed);
        s.total_ns.store(0, std::memory_order_relaxed);
        s.min_ns.store(UINT64_MAX, std::memory_order_relaxed);
        s.max_ns.store(0, std::memory_order_relaxed);
        s.sample_index.store(0, std::memory_order_relaxed);
        for (auto &sample : s.samples) {
            sample.store(0, std::memory_order_relaxed);
        }
    }
}

void printSummary() {
    std::cout
        << "\n╔═══════════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                  Fast Performance Tracer Summary                          ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════════════════════════╣\n";
    std::cout << "║ Key                  │ Count │  Avg(ms) │  P50(ms) │  P95(ms) │  P99(ms) ║\n";
    std::cout << "╠══════════════════════╪═══════╪══════════╪══════════╪══════════╪══════════╣\n";

    for (size_t i = 0; i < static_cast<size_t>(TimingKey::MAX_KEYS); ++i) {
        const auto &stat = getStats()[i];
        uint64_t count = stat.count.load(std::memory_order_relaxed);

        if (count == 0)
            continue;

        std::cout << "║ " << std::left << std::setw(20) << getKeyName(static_cast<TimingKey>(i))
                  << " │ " << std::right << std::setw(5) << count << " │ " << std::setw(8)
                  << std::fixed << std::setprecision(2) << stat.avg_ms() << " │ " << std::setw(8)
                  << std::fixed << std::setprecision(2) << stat.percentile_ms(50.0) << " │ "
                  << std::setw(8) << std::fixed << std::setprecision(2) << stat.percentile_ms(95.0)
                  << " │ " << std::setw(8) << std::fixed << std::setprecision(2)
                  << stat.percentile_ms(99.0) << " ║\n";
    }

    std::cout << "╚═══════════════════════════════════════════════════════════════════════════╝\n";
}

std::string toJSON() {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"fast_tracer\": [\n";

    bool first = true;
    for (size_t i = 0; i < static_cast<size_t>(TimingKey::MAX_KEYS); ++i) {
        const auto &stat = getStats()[i];
        uint64_t count = stat.count.load(std::memory_order_relaxed);

        if (count == 0)
            continue;

        if (!first)
            oss << ",\n";
        first = false;

        oss << "    {\n";
        oss << "      \"key\": \"" << getKeyName(static_cast<TimingKey>(i)) << "\",\n";
        oss << "      \"count\": " << count << ",\n";
        oss << "      \"avg_ms\": " << std::fixed << std::setprecision(3) << stat.avg_ms() << ",\n";
        oss << "      \"p50_ms\": " << stat.percentile_ms(50.0) << ",\n";
        oss << "      \"p95_ms\": " << stat.percentile_ms(95.0) << ",\n";
        oss << "      \"p99_ms\": " << stat.percentile_ms(99.0) << ",\n";
        oss << "      \"min_ms\": " << (stat.min_ns.load(std::memory_order_relaxed) / 1e6) << ",\n";
        oss << "      \"max_ms\": " << (stat.max_ns.load(std::memory_order_relaxed) / 1e6) << "\n";
        oss << "    }";
    }

    oss << "\n  ]\n";
    oss << "}\n";

    return oss.str();
}

} // namespace fast

} // namespace amp
