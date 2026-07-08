/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "perf/PerformanceMetrics.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <limits>
#include <thread>

namespace pek::perf {

namespace {

constexpr std::uint32_t MaxThreadSlots = 32;
constexpr std::uint32_t MaxMetricsPerThread = 256;
constexpr std::uint32_t MaxSpansPerThread = 4096;
constexpr std::uint32_t MaxStackDepth = 64;

std::uint64_t nowNs() noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

std::uint64_t currentThreadId() noexcept {
    return static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

std::uint64_t makeSpanId(std::uint32_t slotIndex, std::uint32_t spanIndex) noexcept {
    return (static_cast<std::uint64_t>(slotIndex) << 32U) | spanIndex;
}

bool copySpanName(std::array<char, PerformanceMetrics::MaxSpanNameLength + 1> &destination,
                  std::string_view source) noexcept {
    const auto sourceLength = source.size();
    const auto copiedLength = std::min(sourceLength, PerformanceMetrics::MaxSpanNameLength);

    std::copy_n(source.data(), copiedLength, destination.begin());
    destination[copiedLength] = '\0';

    return sourceLength > PerformanceMetrics::MaxSpanNameLength;
}

std::string_view
storedNameView(const std::array<char, PerformanceMetrics::MaxSpanNameLength + 1> &name) noexcept {
    return name.data();
}

} // namespace

namespace detail {

// Written by one owner thread, read by snapshot collection.
struct PerformanceMetricsAtomicMetric {
    std::array<char, PerformanceMetrics::MaxSpanNameLength + 1> name{};
    std::atomic<std::uint32_t> depth{0};
    std::atomic<std::uint64_t> count{0};
    std::atomic<std::uint64_t> totalNs{0};
    std::atomic<std::uint64_t> minNs{0};
    std::atomic<std::uint64_t> maxNs{0};
    std::atomic<std::uint64_t> lastNs{0};
    std::atomic<bool> nameTruncated{false};
    std::atomic<bool> hasChildren{false};
};

struct PerformanceMetricsThreadSlot {
    std::atomic<std::uint32_t> generation{0};
    std::uint64_t threadId = 0;
    std::atomic<std::uint32_t> droppedMetrics{0};
    std::uint32_t droppedSpans = 0;
    std::atomic<std::uint32_t> metricCount{0};
    std::array<PerformanceMetricsAtomicMetric, MaxMetricsPerThread> metrics;
    std::vector<PerformanceMetrics::SpanRecord> spans;
};

struct PerformanceMetricsState {
    std::atomic<bool> enabled{true};
    std::atomic<bool> traceEnabled{false};
    std::atomic<std::uint32_t> generation{1};
    std::atomic<std::uint32_t> nextSlot{0};
    std::atomic<bool> threadSlotOverflow{false};
    std::array<PerformanceMetricsThreadSlot, MaxThreadSlots> slots;
};

} // namespace detail

namespace {

// Thread-local view of the current nesting stack for one recorder generation.
struct ThreadFrame {
    detail::PerformanceMetricsState *state = nullptr;
    std::uint32_t generation = 0;
    std::uint32_t slotIndex = 0;
    std::array<std::uint64_t, MaxStackDepth> stack{};
    std::array<std::array<char, PerformanceMetrics::MaxSpanNameLength + 1>, MaxStackDepth>
        metricNames{};
    std::array<bool, MaxStackDepth> metricNameTruncated{};
    std::uint32_t depth = 0;
};

thread_local std::vector<ThreadFrame> threadFrames;
thread_local PerformanceMetrics *currentMetrics = nullptr;

ThreadFrame *findThreadFrame(detail::PerformanceMetricsState *state,
                             std::uint32_t generation) noexcept {
    for (auto &frame : threadFrames) {
        if (frame.state == state && frame.generation == generation) {
            return &frame;
        }
    }
    return nullptr;
}

ThreadFrame *acquireThreadFrame(detail::PerformanceMetricsState *state) {
    const auto generation = state->generation.load(std::memory_order_relaxed);
    if (auto *frame = findThreadFrame(state, generation)) {
        return frame;
    }

    threadFrames.erase(std::remove_if(threadFrames.begin(),
                                      threadFrames.end(),
                                      [state, generation](const ThreadFrame &frame) {
                                          return frame.state == state &&
                                                 frame.generation != generation;
                                      }),
                       threadFrames.end());

    const auto slotIndex = state->nextSlot.fetch_add(1, std::memory_order_relaxed);
    if (slotIndex >= MaxThreadSlots) {
        state->threadSlotOverflow.store(true, std::memory_order_relaxed);
        return nullptr;
    }

    // A reset advances generation, so reused slots are initialized from scratch.
    auto &slot = state->slots[slotIndex];
    slot.generation.store(0, std::memory_order_relaxed);
    slot.threadId = currentThreadId();
    slot.droppedMetrics.store(0, std::memory_order_relaxed);
    slot.droppedSpans = 0;
    slot.metricCount.store(0, std::memory_order_relaxed);
    slot.spans.clear();
    slot.spans.reserve(256);
    slot.generation.store(generation, std::memory_order_release);

    ThreadFrame frame;
    frame.state = state;
    frame.generation = generation;
    frame.slotIndex = slotIndex;
    threadFrames.push_back(frame);
    return &threadFrames.back();
}

detail::PerformanceMetricsAtomicMetric *
ensureMetric(detail::PerformanceMetricsThreadSlot &slot,
             const std::array<char, PerformanceMetrics::MaxSpanNameLength + 1> &name,
             std::uint32_t depth,
             bool nameTruncated) noexcept {
    // Metrics are keyed by stored name and depth, matching Snapshot output.
    const auto nameView = storedNameView(name);
    const auto metricCount =
        std::min(slot.metricCount.load(std::memory_order_acquire), MaxMetricsPerThread);

    for (std::uint32_t index = 0; index < metricCount; ++index) {
        auto &metric = slot.metrics[index];
        if (storedNameView(metric.name) == nameView &&
            metric.depth.load(std::memory_order_relaxed) == depth) {
            if (nameTruncated) {
                metric.nameTruncated.store(true, std::memory_order_relaxed);
            }
            return &metric;
        }
    }

    if (metricCount >= MaxMetricsPerThread) {
        slot.droppedMetrics.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }

    auto &metric = slot.metrics[metricCount];
    metric.name = name;
    metric.depth.store(depth, std::memory_order_relaxed);
    metric.totalNs.store(0, std::memory_order_relaxed);
    metric.minNs.store(0, std::memory_order_relaxed);
    metric.maxNs.store(0, std::memory_order_relaxed);
    metric.lastNs.store(0, std::memory_order_relaxed);
    metric.nameTruncated.store(nameTruncated, std::memory_order_relaxed);
    metric.hasChildren.store(false, std::memory_order_relaxed);
    metric.count.store(0, std::memory_order_release);
    slot.metricCount.store(metricCount + 1, std::memory_order_release);
    return &metric;
}

void markMetricHasChildren(detail::PerformanceMetricsThreadSlot &slot,
                           const std::array<char, PerformanceMetrics::MaxSpanNameLength + 1> &name,
                           std::uint32_t depth,
                           bool nameTruncated) noexcept {
    auto *metric = ensureMetric(slot, name, depth, nameTruncated);
    if (metric != nullptr) {
        metric->hasChildren.store(true, std::memory_order_relaxed);
    }
}

void updateMetric(detail::PerformanceMetricsThreadSlot &slot,
                  const std::array<char, PerformanceMetrics::MaxSpanNameLength + 1> &name,
                  std::uint64_t durationNs,
                  std::uint32_t depth,
                  bool nameTruncated) noexcept {
    auto *metric = ensureMetric(slot, name, depth, nameTruncated);
    if (metric == nullptr) {
        return;
    }

    if (metric->count.load(std::memory_order_relaxed) == 0) {
        metric->totalNs.store(durationNs, std::memory_order_relaxed);
        metric->minNs.store(durationNs, std::memory_order_relaxed);
        metric->maxNs.store(durationNs, std::memory_order_relaxed);
        metric->lastNs.store(durationNs, std::memory_order_relaxed);
        metric->count.store(1, std::memory_order_release);
        return;
    }

    metric->totalNs.fetch_add(durationNs, std::memory_order_relaxed);

    auto minNs = metric->minNs.load(std::memory_order_relaxed);
    while (durationNs < minNs &&
           !metric->minNs.compare_exchange_weak(minNs, durationNs, std::memory_order_relaxed)) {
    }

    auto maxNs = metric->maxNs.load(std::memory_order_relaxed);
    while (durationNs > maxNs &&
           !metric->maxNs.compare_exchange_weak(maxNs, durationNs, std::memory_order_relaxed)) {
    }

    metric->lastNs.store(durationNs, std::memory_order_relaxed);
    metric->count.fetch_add(1, std::memory_order_release);
}

void mergeMetric(std::vector<PerformanceMetrics::MetricRecord> &metrics,
                 const PerformanceMetrics::MetricRecord &source) {
    if (source.count == 0) {
        return;
    }

    const auto sourceName = source.nameView();
    for (auto &metric : metrics) {
        if (metric.nameView() == sourceName && metric.depth == source.depth) {
            metric.count += source.count;
            metric.totalNs += source.totalNs;
            metric.minNs = std::min(metric.minNs, source.minNs);
            metric.maxNs = std::max(metric.maxNs, source.maxNs);
            metric.lastNs = source.lastNs;
            metric.nameTruncated = metric.nameTruncated || source.nameTruncated;
            metric.hasChildren = metric.hasChildren || source.hasChildren;
            return;
        }
    }

    metrics.push_back(source);
}

void collectMetrics(std::vector<PerformanceMetrics::MetricRecord> &destination,
                    const detail::PerformanceMetricsThreadSlot &slot) {
    const auto metricCount =
        std::min(slot.metricCount.load(std::memory_order_acquire), MaxMetricsPerThread);

    for (std::uint32_t index = 0; index < metricCount; ++index) {
        const auto &source = slot.metrics[index];
        PerformanceMetrics::MetricRecord metric;
        metric.count = source.count.load(std::memory_order_acquire);
        if (metric.count == 0) {
            continue;
        }

        metric.name = source.name;
        metric.depth = source.depth.load(std::memory_order_relaxed);
        metric.totalNs = source.totalNs.load(std::memory_order_relaxed);
        metric.minNs = source.minNs.load(std::memory_order_relaxed);
        metric.maxNs = source.maxNs.load(std::memory_order_relaxed);
        metric.lastNs = source.lastNs.load(std::memory_order_relaxed);
        metric.nameTruncated = source.nameTruncated.load(std::memory_order_relaxed);
        metric.hasChildren = source.hasChildren.load(std::memory_order_relaxed);

        mergeMetric(destination, metric);
    }
}

} // namespace

PerformanceMetrics::Scope::Scope(PerformanceMetrics *metrics,
                                 std::uint32_t generation,
                                 std::uint32_t slotIndex,
                                 std::uint32_t spanIndex,
                                 std::array<char, MaxSpanNameLength + 1> name,
                                 std::uint64_t startNs,
                                 std::uint32_t depth,
                                 bool nameTruncated,
                                 bool traceRecorded) noexcept
    : metrics(metrics), generation(generation), slotIndex(slotIndex), spanIndex(spanIndex),
      name(name), startNs(startNs), depth(depth), nameTruncated(nameTruncated),
      traceRecorded(traceRecorded) {}

PerformanceMetrics::Scope::Scope(Scope &&other) noexcept
    : metrics(other.metrics), generation(other.generation), slotIndex(other.slotIndex),
      spanIndex(other.spanIndex), name(other.name), startNs(other.startNs), depth(other.depth),
      nameTruncated(other.nameTruncated), traceRecorded(other.traceRecorded) {
    other.metrics = nullptr;
}

PerformanceMetrics::Scope &PerformanceMetrics::Scope::operator=(Scope &&other) noexcept {
    if (this != &other) {
        close();
        metrics = other.metrics;
        generation = other.generation;
        slotIndex = other.slotIndex;
        spanIndex = other.spanIndex;
        name = other.name;
        startNs = other.startNs;
        depth = other.depth;
        nameTruncated = other.nameTruncated;
        traceRecorded = other.traceRecorded;
        other.metrics = nullptr;
    }
    return *this;
}

PerformanceMetrics::Scope::~Scope() {
    close();
}

void PerformanceMetrics::Scope::close() noexcept {
    if (metrics == nullptr) {
        return;
    }

    metrics->exitBlock(
        generation, slotIndex, spanIndex, name, startNs, depth, nameTruncated, traceRecorded);
    metrics = nullptr;
}

PerformanceMetrics::PerformanceMetrics()
    : state(std::make_unique<detail::PerformanceMetricsState>()) {}

PerformanceMetrics::~PerformanceMetrics() = default;

PerformanceMetrics::Scope PerformanceMetrics::scope(std::string_view name) noexcept {
    if (!state->enabled.load(std::memory_order_relaxed)) {
        return {};
    }

    try {
        auto *frame = acquireThreadFrame(state.get());
        if (frame == nullptr) {
            return {};
        }

        auto &slot = state->slots[frame->slotIndex];
        if (frame->depth >= MaxStackDepth) {
            ++slot.droppedSpans;
            return {};
        }

        std::array<char, MaxSpanNameLength + 1> storedName{};
        const bool nameTruncated = copySpanName(storedName, name);
        const auto startNs = nowNs();
        const auto depth = frame->depth;
        auto spanIndex = std::numeric_limits<std::uint32_t>::max();
        auto spanId = InvalidSpanId;
        bool traceRecorded = false;

        // Aggregate counters are always recorded; exact spans are optional.
        if (state->traceEnabled.load(std::memory_order_relaxed)) {
            if (slot.spans.size() >= MaxSpansPerThread) {
                ++slot.droppedSpans;
            } else {
                spanIndex = static_cast<std::uint32_t>(slot.spans.size());
                spanId = makeSpanId(frame->slotIndex, spanIndex);
                const auto parentId =
                    frame->depth == 0 ? InvalidSpanId : frame->stack[frame->depth - 1];

                SpanRecord record;
                record.id = spanId;
                record.parentId = parentId;
                record.name = storedName;
                record.nameTruncated = nameTruncated;
                record.startNs = startNs;
                record.threadId = slot.threadId;
                record.depth = depth;

                slot.spans.push_back(record);
                traceRecorded = true;
            }
        }

        if (frame->depth > 0) {
            const auto parentDepth = frame->depth - 1U;
            markMetricHasChildren(slot,
                                  frame->metricNames[parentDepth],
                                  parentDepth,
                                  frame->metricNameTruncated[parentDepth]);
        }

        frame->stack[frame->depth] = spanId;
        frame->metricNames[frame->depth] = storedName;
        frame->metricNameTruncated[frame->depth] = nameTruncated;
        ++frame->depth;

        return Scope(this,
                     frame->generation,
                     frame->slotIndex,
                     spanIndex,
                     storedName,
                     startNs,
                     depth,
                     nameTruncated,
                     traceRecorded);
    } catch (...) {
        return {};
    }
}

void PerformanceMetrics::setEnabled(bool enabled) noexcept {
    state->enabled.store(enabled, std::memory_order_relaxed);
}

bool PerformanceMetrics::enabled() const noexcept {
    return state->enabled.load(std::memory_order_relaxed);
}

void PerformanceMetrics::setTraceEnabled(bool enabled) noexcept {
    state->traceEnabled.store(enabled, std::memory_order_relaxed);
}

bool PerformanceMetrics::traceEnabled() const noexcept {
    return state->traceEnabled.load(std::memory_order_relaxed);
}

PerformanceMetrics::Snapshot PerformanceMetrics::snapshot() const {
    auto snapshot = aggregateSnapshot();
    const auto generation = state->generation.load(std::memory_order_relaxed);
    const auto usedSlots =
        std::min(state->nextSlot.load(std::memory_order_relaxed), MaxThreadSlots);

    for (std::uint32_t slotIndex = 0; slotIndex < usedSlots; ++slotIndex) {
        const auto &slot = state->slots[slotIndex];
        if (slot.generation.load(std::memory_order_acquire) != generation) {
            continue;
        }

        snapshot.droppedSpans += slot.droppedSpans;
        snapshot.spans.insert(snapshot.spans.end(), slot.spans.begin(), slot.spans.end());
    }

    std::sort(snapshot.spans.begin(),
              snapshot.spans.end(),
              [](const SpanRecord &left, const SpanRecord &right) {
                  if (left.startNs != right.startNs) {
                      return left.startNs < right.startNs;
                  }
                  return left.id < right.id;
              });

    return snapshot;
}

PerformanceMetrics::Snapshot PerformanceMetrics::aggregateSnapshot() const {
    Snapshot snapshot;
    const auto generation = state->generation.load(std::memory_order_relaxed);
    const auto usedSlots =
        std::min(state->nextSlot.load(std::memory_order_relaxed), MaxThreadSlots);

    for (std::uint32_t slotIndex = 0; slotIndex < usedSlots; ++slotIndex) {
        const auto &slot = state->slots[slotIndex];
        if (slot.generation.load(std::memory_order_acquire) != generation) {
            continue;
        }

        snapshot.droppedMetrics += slot.droppedMetrics.load(std::memory_order_relaxed);
        collectMetrics(snapshot.metrics, slot);
    }

    snapshot.threadSlotOverflow = state->threadSlotOverflow.load(std::memory_order_relaxed);

    std::sort(snapshot.metrics.begin(),
              snapshot.metrics.end(),
              [](const MetricRecord &left, const MetricRecord &right) {
                  if (left.depth != right.depth) {
                      return left.depth < right.depth;
                  }
                  return left.nameView() < right.nameView();
              });

    return snapshot;
}

void PerformanceMetrics::reset() {
    // Old thread-local frames become invisible once generation changes.
    state->generation.fetch_add(1, std::memory_order_relaxed);
    state->nextSlot.store(0, std::memory_order_relaxed);
    state->threadSlotOverflow.store(false, std::memory_order_relaxed);

    for (auto &slot : state->slots) {
        slot.generation.store(0, std::memory_order_relaxed);
        slot.threadId = 0;
        slot.droppedMetrics.store(0, std::memory_order_relaxed);
        slot.droppedSpans = 0;
        slot.metricCount.store(0, std::memory_order_relaxed);
        slot.spans.clear();
    }
}

void PerformanceMetrics::exitBlock(std::uint32_t generation,
                                   std::uint32_t slotIndex,
                                   std::uint32_t spanIndex,
                                   const std::array<char, MaxSpanNameLength + 1> &name,
                                   std::uint64_t startNs,
                                   std::uint32_t depth,
                                   bool nameTruncated,
                                   bool traceRecorded) noexcept {
    auto *frame = findThreadFrame(state.get(), generation);
    if (frame == nullptr || frame->slotIndex != slotIndex || slotIndex >= MaxThreadSlots) {
        return;
    }

    const auto endNs = nowNs();
    auto &slot = state->slots[slotIndex];
    if (traceRecorded && spanIndex < slot.spans.size() && slot.spans[spanIndex].endNs == 0) {
        slot.spans[spanIndex].endNs = endNs;
    }

    if (endNs >= startNs) {
        updateMetric(slot, name, endNs - startNs, depth, nameTruncated);
    }

    if (frame->depth <= depth) {
        return;
    }

    if (!traceRecorded) {
        frame->depth = depth;
        return;
    }

    const auto spanId = makeSpanId(slotIndex, spanIndex);
    if (frame->stack[frame->depth - 1] == spanId) {
        frame->depth = depth;
        return;
    }

    for (std::uint32_t index = frame->depth; index > 0; --index) {
        if (frame->stack[index - 1] == spanId) {
            frame->depth = index - 1;
            return;
        }
    }
}

ScopedMetricsContext::ScopedMetricsContext(PerformanceMetrics &metrics) noexcept
    : previous(currentMetrics) {
    currentMetrics = &metrics;
}

ScopedMetricsContext::~ScopedMetricsContext() {
    currentMetrics = previous;
}

PerformanceMetrics *currentPerformanceMetrics() noexcept {
    return currentMetrics;
}

PerformanceMetrics &defaultPerformanceMetrics() noexcept {
    static PerformanceMetrics metrics;
    return metrics;
}

PerformanceMetrics::Scope enterBlock(std::string_view name) noexcept {
    return defaultPerformanceMetrics().scope(name);
}

PerformanceMetrics::Scope enterCurrentBlock(std::string_view name) noexcept {
    auto *metrics = currentPerformanceMetrics();
    if (metrics == nullptr) {
        return {};
    }

    return metrics->scope(name);
}

} // namespace pek::perf
