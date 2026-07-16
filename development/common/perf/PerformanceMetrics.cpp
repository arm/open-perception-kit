/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "perf/PerformanceMetrics.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>

namespace pek::perf {

namespace {

constexpr std::uint32_t MaxThreadSlots = 128;
constexpr std::uint32_t MaxMetricsPerThread = 1024;
constexpr std::uint32_t MaxHistoryEventsPerChunk = 4096;
constexpr std::uint32_t MaxStackDepth = 64;
constexpr std::uint32_t InvalidLocalMetricIndex = std::numeric_limits<std::uint32_t>::max();

std::uint64_t nextInstanceId() noexcept {
    static std::atomic<std::uint64_t> nextId{1};
    return nextId.fetch_add(1, std::memory_order_relaxed);
}

std::uint64_t nowNs() noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

std::uint64_t currentThreadId() noexcept {
    return std::hash<std::thread::id>{}(std::this_thread::get_id());
}

bool copySpanName(std::array<char, PerformanceMetrics::MaxSpanNameLength + 1> &destination,
                  std::string_view source) noexcept {
    const auto sourceLength = source.size();
    const auto copiedLength = std::min(sourceLength, PerformanceMetrics::MaxSpanNameLength);

    std::copy_n(source.begin(), copiedLength, destination.begin());
    destination[copiedLength] = '\0';

    return sourceLength > PerformanceMetrics::MaxSpanNameLength;
}

std::string_view
storedNameView(const std::array<char, PerformanceMetrics::MaxSpanNameLength + 1> &name) noexcept {
    return name.data();
}

bool namesEqual(const std::array<char, PerformanceMetrics::MaxSpanNameLength + 1> &left,
                const std::array<char, PerformanceMetrics::MaxSpanNameLength + 1> &right) noexcept {
    return storedNameView(left) == storedNameView(right);
}

void writeCsvEscapedName(std::FILE *file, std::string_view name) noexcept {
    std::fputc('"', file);
    for (const char c : name) {
        if (c == '"') {
            std::fputc('"', file);
        }
        std::fputc(c, file);
    }
    std::fputc('"', file);
}

} // namespace

namespace detail {

struct HistoryChunk {
    std::array<PerformanceMetrics::SpanRecord, MaxHistoryEventsPerChunk> records;
    std::size_t size = 0;
};

// Written by one owner thread, read by snapshot collection.
struct PerformanceMetricsAtomicMetric {
    std::atomic<std::uint64_t> sequence{0};
    std::array<char, PerformanceMetrics::MaxSpanNameLength + 1> name{};
    std::uint32_t parentIndex = InvalidLocalMetricIndex;
    std::uint32_t depth = 0;
    std::atomic<std::uint64_t> count{0};
    std::atomic<std::uint64_t> totalNs{0};
    std::atomic<std::uint64_t> minNs{0};
    std::atomic<std::uint64_t> maxNs{0};
    std::atomic<std::uint64_t> lastNs{0};
    std::atomic<bool> nameTruncated{false};
    std::atomic<bool> hasChildren{false};
};

struct PerformanceMetricsThreadSlot {
    std::atomic<bool> active{false};
    std::uint64_t threadId = 0;
    std::atomic<std::uint32_t> droppedMetrics{0};
    std::atomic<std::uint32_t> droppedSpans{0};
    std::atomic<std::uint32_t> droppedHistoryEvents{0};
    std::atomic<std::uint32_t> wrongThreadScopeCloses{0};
    std::atomic<std::uint32_t> metricCount{0};
    std::array<PerformanceMetricsAtomicMetric, MaxMetricsPerThread> metrics;
    mutable std::mutex historyMutex;
    std::vector<std::unique_ptr<HistoryChunk>> historyChunks;
};

struct PerformanceMetricsState {
    explicit PerformanceMetricsState(std::uint64_t id) : instanceId(id) {}

    const std::uint64_t instanceId = 0;
    std::atomic<bool> enabled{true};
    std::atomic<bool> historyEnabled{false};
    std::atomic<std::uint32_t> nextSlot{0};
    std::atomic<std::uint64_t> nextSpanId{1};
    std::atomic<bool> threadSlotOverflow{false};
    std::array<PerformanceMetricsThreadSlot, MaxThreadSlots> slots;
    mutable std::mutex autoCsvMutex;
    std::string autoCsvPath;
};

void PerformanceMetricsStateDeleter::operator()(PerformanceMetricsState *state) const noexcept {
    std::default_delete<PerformanceMetricsState>{}(state);
}

} // namespace detail

namespace {

struct StackEntry {
    std::uint32_t metricIndex = InvalidLocalMetricIndex;
    std::uint64_t spanId = PerformanceMetrics::InvalidSpanId;
};

// Thread-local view of the current nesting stack for one recorder instance.
struct ThreadFrame {
    detail::PerformanceMetricsState *state = nullptr;
    std::uint64_t instanceId = 0;
    std::uint32_t slotIndex = 0;
    std::array<StackEntry, MaxStackDepth> stack{};
    std::uint32_t depth = 0;
};

struct ThreadContext {
    std::vector<ThreadFrame> frames;
    PerformanceMetrics *currentMetrics = nullptr;
};

ThreadContext &threadContext() noexcept {
    thread_local ThreadContext context;
    return context;
}

ThreadFrame *findThreadFrame(const detail::PerformanceMetricsState *state) noexcept {
    for (auto &frame : threadContext().frames) {
        if (frame.state == state && frame.instanceId == state->instanceId) {
            return &frame;
        }
    }
    return nullptr;
}

ThreadFrame *acquireThreadFrame(detail::PerformanceMetricsState *state) {
    if (auto *frame = findThreadFrame(state)) {
        return frame;
    }

    auto &frames = threadContext().frames;
    std::erase_if(frames, [state](const ThreadFrame &frame) {
        return frame.state == state && frame.instanceId != state->instanceId;
    });

    const auto slotIndex = state->nextSlot.fetch_add(1, std::memory_order_acq_rel);
    if (slotIndex >= MaxThreadSlots) {
        state->threadSlotOverflow.store(true, std::memory_order_release);
        return nullptr;
    }

    auto &slot = state->slots[slotIndex];
    slot.threadId = currentThreadId();
    slot.droppedMetrics.store(0, std::memory_order_relaxed);
    slot.droppedSpans.store(0, std::memory_order_relaxed);
    slot.droppedHistoryEvents.store(0, std::memory_order_relaxed);
    slot.wrongThreadScopeCloses.store(0, std::memory_order_relaxed);
    slot.metricCount.store(0, std::memory_order_relaxed);
    slot.active.store(true, std::memory_order_release);

    ThreadFrame frame;
    frame.state = state;
    frame.instanceId = state->instanceId;
    frame.slotIndex = slotIndex;
    frames.push_back(frame);
    return &frames.back();
}

std::uint32_t ensureMetric(detail::PerformanceMetricsThreadSlot &slot,
                           const std::array<char, PerformanceMetrics::MaxSpanNameLength + 1> &name,
                           std::uint32_t parentIndex,
                           std::uint32_t depth,
                           bool nameTruncated) noexcept {
    const auto metricCount =
        std::min(slot.metricCount.load(std::memory_order_acquire), MaxMetricsPerThread);

    for (std::uint32_t index = 0; index < metricCount; ++index) {
        auto &metric = slot.metrics[index];
        if (metric.parentIndex == parentIndex && namesEqual(metric.name, name)) {
            if (nameTruncated) {
                metric.nameTruncated.store(true, std::memory_order_relaxed);
            }
            return index;
        }
    }

    if (metricCount >= MaxMetricsPerThread) {
        slot.droppedMetrics.fetch_add(1, std::memory_order_relaxed);
        return InvalidLocalMetricIndex;
    }

    auto &metric = slot.metrics[metricCount];
    metric.sequence.store(0, std::memory_order_relaxed);
    metric.name = name;
    metric.parentIndex = parentIndex;
    metric.depth = depth;
    metric.count.store(0, std::memory_order_relaxed);
    metric.totalNs.store(0, std::memory_order_relaxed);
    metric.minNs.store(0, std::memory_order_relaxed);
    metric.maxNs.store(0, std::memory_order_relaxed);
    metric.lastNs.store(0, std::memory_order_relaxed);
    metric.nameTruncated.store(nameTruncated, std::memory_order_relaxed);
    metric.hasChildren.store(false, std::memory_order_relaxed);
    slot.metricCount.store(metricCount + 1, std::memory_order_release);
    return metricCount;
}

void beginMetricWrite(detail::PerformanceMetricsAtomicMetric &metric) noexcept {
    metric.sequence.fetch_add(1, std::memory_order_acq_rel);
}

void endMetricWrite(detail::PerformanceMetricsAtomicMetric &metric) noexcept {
    metric.sequence.fetch_add(1, std::memory_order_release);
}

void markMetricHasChildren(detail::PerformanceMetricsThreadSlot &slot,
                           std::uint32_t metricIndex) noexcept {
    if (metricIndex >= MaxMetricsPerThread) {
        return;
    }

    auto &metric = slot.metrics[metricIndex];
    beginMetricWrite(metric);
    metric.hasChildren.store(true, std::memory_order_relaxed);
    endMetricWrite(metric);
}

void updateMetric(detail::PerformanceMetricsThreadSlot &slot,
                  std::uint32_t metricIndex,
                  std::uint64_t durationNs) noexcept {
    if (metricIndex >= MaxMetricsPerThread) {
        return;
    }

    auto &metric = slot.metrics[metricIndex];
    beginMetricWrite(metric);

    const auto count = metric.count.load(std::memory_order_relaxed);
    const auto totalNs = metric.totalNs.load(std::memory_order_relaxed);
    const auto minNs = metric.minNs.load(std::memory_order_relaxed);
    const auto maxNs = metric.maxNs.load(std::memory_order_relaxed);

    metric.totalNs.store(totalNs + durationNs, std::memory_order_relaxed);
    metric.minNs.store(count == 0 ? durationNs : std::min(minNs, durationNs),
                       std::memory_order_relaxed);
    metric.maxNs.store(count == 0 ? durationNs : std::max(maxNs, durationNs),
                       std::memory_order_relaxed);
    metric.lastNs.store(durationNs, std::memory_order_relaxed);
    metric.count.store(count + 1, std::memory_order_relaxed);

    endMetricWrite(metric);
}

PerformanceMetrics::MetricRecord readMetric(const detail::PerformanceMetricsThreadSlot &slot,
                                            std::uint32_t metricIndex) noexcept {
    const auto &source = slot.metrics[metricIndex];
    PerformanceMetrics::MetricRecord metric;

    for (;;) {
        const auto sequenceBefore = source.sequence.load(std::memory_order_acquire);
        if ((sequenceBefore & 1U) != 0) {
            std::this_thread::yield();
            continue;
        }

        metric.name = source.name;
        metric.depth = source.depth;
        metric.count = source.count.load(std::memory_order_relaxed);
        metric.totalNs = source.totalNs.load(std::memory_order_relaxed);
        metric.minNs = source.minNs.load(std::memory_order_relaxed);
        metric.maxNs = source.maxNs.load(std::memory_order_relaxed);
        metric.lastNs = source.lastNs.load(std::memory_order_relaxed);
        metric.nameTruncated = source.nameTruncated.load(std::memory_order_relaxed);
        metric.hasChildren = source.hasChildren.load(std::memory_order_relaxed);

        const auto sequenceAfter = source.sequence.load(std::memory_order_acquire);
        if (sequenceBefore == sequenceAfter && (sequenceAfter & 1U) == 0) {
            break;
        }
    }

    metric.averageNs = metric.count == 0 ? 0 : metric.totalNs / metric.count;
    return metric;
}

std::uint64_t mergeMetric(std::vector<PerformanceMetrics::MetricRecord> &destination,
                          const PerformanceMetrics::MetricRecord &source,
                          std::uint64_t parentId) {
    for (auto &metric : destination) {
        if (metric.parentId == parentId && namesEqual(metric.name, source.name)) {
            metric.count += source.count;
            metric.totalNs += source.totalNs;
            if (source.count > 0) {
                metric.minNs = metric.count == source.count ? source.minNs
                                                            : std::min(metric.minNs, source.minNs);
                metric.maxNs = std::max(metric.maxNs, source.maxNs);
                metric.lastNs = source.lastNs;
            }
            metric.averageNs = metric.count == 0 ? 0 : metric.totalNs / metric.count;
            metric.nameTruncated = metric.nameTruncated || source.nameTruncated;
            metric.hasChildren = metric.hasChildren || source.hasChildren;
            return metric.id;
        }
    }

    auto metric = source;
    metric.id = destination.size();
    metric.parentId = parentId;
    metric.averageNs = metric.count == 0 ? 0 : metric.totalNs / metric.count;
    destination.push_back(metric);
    return metric.id;
}

std::uint64_t collectMetric(std::vector<PerformanceMetrics::MetricRecord> &destination,
                            const detail::PerformanceMetricsThreadSlot &slot,
                            std::uint32_t metricIndex,
                            std::uint32_t metricCount,
                            std::vector<std::uint64_t> &mergedIds) {
    if (metricIndex >= metricCount) {
        return PerformanceMetrics::InvalidMetricId;
    }

    if (mergedIds[metricIndex] != PerformanceMetrics::InvalidMetricId) {
        return mergedIds[metricIndex];
    }

    const auto &sourceMetadata = slot.metrics[metricIndex];
    std::uint64_t parentId = PerformanceMetrics::InvalidMetricId;
    if (sourceMetadata.parentIndex != InvalidLocalMetricIndex &&
        sourceMetadata.parentIndex < metricCount) {
        parentId =
            collectMetric(destination, slot, sourceMetadata.parentIndex, metricCount, mergedIds);
    }

    const auto source = readMetric(slot, metricIndex);
    if (source.count == 0 && !source.hasChildren) {
        return PerformanceMetrics::InvalidMetricId;
    }

    const auto mergedId = mergeMetric(destination, source, parentId);
    mergedIds[metricIndex] = mergedId;
    return mergedId;
}

void collectMetrics(std::vector<PerformanceMetrics::MetricRecord> &destination,
                    const detail::PerformanceMetricsThreadSlot &slot) {
    const auto metricCount =
        std::min(slot.metricCount.load(std::memory_order_acquire), MaxMetricsPerThread);
    std::vector<std::uint64_t> mergedIds(metricCount, PerformanceMetrics::InvalidMetricId);

    for (std::uint32_t index = 0; index < metricCount; ++index) {
        collectMetric(destination, slot, index, metricCount, mergedIds);
    }
}

void appendHistoryEvent(detail::PerformanceMetricsThreadSlot &slot,
                        const PerformanceMetrics::SpanRecord &record) noexcept {
    try {
        std::scoped_lock lock(slot.historyMutex);
        if (slot.historyChunks.empty() ||
            slot.historyChunks.back()->size >= MaxHistoryEventsPerChunk) {
            slot.historyChunks.push_back(std::make_unique<detail::HistoryChunk>());
        }

        auto &chunk = *slot.historyChunks.back();
        chunk.records[chunk.size] = record;
        ++chunk.size;
    } catch (...) {
        slot.droppedHistoryEvents.fetch_add(1, std::memory_order_relaxed);
    }
}

void collectHistoryEvents(std::vector<PerformanceMetrics::SpanRecord> &destination,
                          const detail::PerformanceMetricsThreadSlot &slot) {
    std::scoped_lock lock(slot.historyMutex);
    for (const auto &chunk : slot.historyChunks) {
        destination.insert(
            destination.end(), chunk->records.begin(), chunk->records.begin() + chunk->size);
    }
}

void writeDefaultMetricsAtExit() noexcept {
    defaultPerformanceMetrics().writeAutoCsv();
}

} // namespace

PerformanceMetrics::Scope::Scope(Recording recording) noexcept : recording(recording) {}

PerformanceMetrics::Scope::Scope(Scope &&other) noexcept : recording(other.recording) {
    other.recording.metrics = nullptr;
}

PerformanceMetrics::Scope &PerformanceMetrics::Scope::operator=(Scope &&other) noexcept {
    if (this != &other) {
        close();
        recording = other.recording;
        other.recording.metrics = nullptr;
    }
    return *this;
}

PerformanceMetrics::Scope::~Scope() {
    close();
}

void PerformanceMetrics::Scope::close() noexcept {
    if (recording.metrics == nullptr) {
        return;
    }

    recording.metrics->exitBlock(recording.slotIndex,
                                 recording.metricIndex,
                                 recording.spanId,
                                 recording.parentSpanId,
                                 recording.startNs,
                                 recording.depth,
                                 recording.historyRecorded);
    recording.metrics = nullptr;
}

detail::PerformanceMetricsStatePtr PerformanceMetrics::createState() {
    auto state = std::make_unique<detail::PerformanceMetricsState>(nextInstanceId());
    return detail::PerformanceMetricsStatePtr(state.release());
}

PerformanceMetrics::PerformanceMetrics() = default;

PerformanceMetrics::~PerformanceMetrics() {
    writeAutoCsv();
}

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
            slot.droppedSpans.fetch_add(1, std::memory_order_relaxed);
            return {};
        }

        std::array<char, MaxSpanNameLength + 1> storedName{};
        const bool nameTruncated = copySpanName(storedName, name);
        const auto parentIndex = frame->depth == 0 ? InvalidLocalMetricIndex
                                                   : frame->stack[frame->depth - 1].metricIndex;
        const auto metricIndex =
            ensureMetric(slot, storedName, parentIndex, frame->depth, nameTruncated);
        if (metricIndex == InvalidLocalMetricIndex) {
            return {};
        }

        if (parentIndex != InvalidLocalMetricIndex) {
            markMetricHasChildren(slot, parentIndex);
        }

        const auto historyRecorded = state->historyEnabled.load(std::memory_order_relaxed);
        const auto spanId = historyRecorded
                                ? state->nextSpanId.fetch_add(1, std::memory_order_relaxed)
                                : InvalidSpanId;
        const auto parentSpanId =
            frame->depth == 0 ? InvalidSpanId : frame->stack[frame->depth - 1].spanId;
        const auto startNs = nowNs();
        const auto depth = frame->depth;

        frame->stack[frame->depth] = StackEntry{metricIndex, spanId};
        ++frame->depth;

        return Scope(Scope::Recording{this,
                                      frame->slotIndex,
                                      metricIndex,
                                      spanId,
                                      parentSpanId,
                                      startNs,
                                      depth,
                                      historyRecorded});
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

void PerformanceMetrics::setHistoryEnabled(bool enabled) noexcept {
    state->historyEnabled.store(enabled, std::memory_order_relaxed);
}

bool PerformanceMetrics::historyEnabled() const noexcept {
    return state->historyEnabled.load(std::memory_order_relaxed);
}

void PerformanceMetrics::setTraceEnabled(bool enabled) noexcept {
    setHistoryEnabled(enabled);
}

bool PerformanceMetrics::traceEnabled() const noexcept {
    return historyEnabled();
}

void PerformanceMetrics::setAutoCsvExportPath(std::string_view path) {
    std::scoped_lock lock(state->autoCsvMutex);
    state->autoCsvPath.assign(path.begin(), path.end());
}

std::string PerformanceMetrics::autoCsvExportPath() const {
    std::scoped_lock lock(state->autoCsvMutex);
    return state->autoCsvPath;
}

bool PerformanceMetrics::writeCsv(std::string_view path) const {
    const std::string outputPath(path.begin(), path.end());
    auto *file = std::fopen(outputPath.c_str(), "w");
    if (file == nullptr) {
        return false;
    }

    std::vector<SpanRecord> spans;
    const auto usedSlots =
        std::min(state->nextSlot.load(std::memory_order_acquire), MaxThreadSlots);
    for (std::uint32_t slotIndex = 0; slotIndex < usedSlots; ++slotIndex) {
        const auto &slot = state->slots[slotIndex];
        if (!slot.active.load(std::memory_order_acquire)) {
            continue;
        }
        collectHistoryEvents(spans, slot);
    }

    std::ranges::sort(spans, [](const SpanRecord &left, const SpanRecord &right) {
        if (left.startNs != right.startNs) {
            return left.startNs < right.startNs;
        }
        return left.id < right.id;
    });

    std::fprintf(file,
                 "thread_id,span_id,parent_span_id,depth,name,start_ns,end_ns,duration_ns,"
                 "duration_ms\n");
    for (const auto &span : spans) {
        if (!span.complete()) {
            continue;
        }

        std::fprintf(file,
                     "%llu,%llu,%llu,%u,",
                     static_cast<unsigned long long>(span.threadId),
                     static_cast<unsigned long long>(span.id),
                     static_cast<unsigned long long>(span.parentId),
                     span.depth);
        writeCsvEscapedName(file, span.nameView());
        std::fprintf(file,
                     ",%llu,%llu,%llu,%.2f\n",
                     static_cast<unsigned long long>(span.startNs),
                     static_cast<unsigned long long>(span.endNs),
                     static_cast<unsigned long long>(span.durationNs()),
                     static_cast<double>(span.durationNs()) / 1000000.0);
    }

    const bool ok = std::fclose(file) == 0;
    return ok;
}

void PerformanceMetrics::writeAutoCsv() const noexcept {
    try {
        const auto path = autoCsvExportPath();
        if (!path.empty()) {
            if (!writeCsv(path)) {
                std::fputs("Performance metrics CSV export failed.\n", stderr);
            }
        }
    } catch (...) {
        std::fputs("Performance metrics CSV export failed with an unexpected error.\n", stderr);
    }
}

PerformanceMetrics::Snapshot PerformanceMetrics::aggregateSnapshot() const {
    Snapshot snapshot;
    const auto usedSlots =
        std::min(state->nextSlot.load(std::memory_order_acquire), MaxThreadSlots);

    for (std::uint32_t slotIndex = 0; slotIndex < usedSlots; ++slotIndex) {
        const auto &slot = state->slots[slotIndex];
        if (!slot.active.load(std::memory_order_acquire)) {
            continue;
        }

        snapshot.droppedMetrics += slot.droppedMetrics.load(std::memory_order_relaxed);
        snapshot.droppedSpans += slot.droppedSpans.load(std::memory_order_relaxed);
        snapshot.droppedHistoryEvents += slot.droppedHistoryEvents.load(std::memory_order_relaxed);
        snapshot.wrongThreadScopeCloses +=
            slot.wrongThreadScopeCloses.load(std::memory_order_relaxed);
        collectMetrics(snapshot.metrics, slot);
    }

    snapshot.threadSlotOverflow = state->threadSlotOverflow.load(std::memory_order_acquire);
    return snapshot;
}

PerformanceMetrics::Snapshot PerformanceMetrics::snapshot() const {
    auto snapshot = aggregateSnapshot();
    const auto usedSlots =
        std::min(state->nextSlot.load(std::memory_order_acquire), MaxThreadSlots);

    for (std::uint32_t slotIndex = 0; slotIndex < usedSlots; ++slotIndex) {
        const auto &slot = state->slots[slotIndex];
        if (!slot.active.load(std::memory_order_acquire)) {
            continue;
        }

        collectHistoryEvents(snapshot.spans, slot);
    }

    std::ranges::sort(snapshot.spans, [](const SpanRecord &left, const SpanRecord &right) {
        if (left.startNs != right.startNs) {
            return left.startNs < right.startNs;
        }
        return left.id < right.id;
    });

    return snapshot;
}

void PerformanceMetrics::exitBlock(std::uint32_t slotIndex,
                                   std::uint32_t metricIndex,
                                   std::uint64_t spanId,
                                   std::uint64_t parentSpanId,
                                   std::uint64_t startNs,
                                   std::uint32_t depth,
                                   bool historyRecorded) noexcept {
    if (slotIndex >= MaxThreadSlots) {
        return;
    }

    auto &slot = state->slots[slotIndex];
    auto *frame = findThreadFrame(state.get());
    if (frame == nullptr || frame->slotIndex != slotIndex) {
        slot.wrongThreadScopeCloses.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const auto endNs = nowNs();
    if (endNs >= startNs) {
        updateMetric(slot, metricIndex, endNs - startNs);
    }

    if (historyRecorded && endNs >= startNs && metricIndex < MaxMetricsPerThread) {
        const auto &metric = slot.metrics[metricIndex];
        SpanRecord record;
        record.id = spanId;
        record.parentId = parentSpanId;
        record.name = metric.name;
        record.nameTruncated = metric.nameTruncated.load(std::memory_order_relaxed);
        record.startNs = startNs;
        record.endNs = endNs;
        record.threadId = slot.threadId;
        record.depth = depth;
        appendHistoryEvent(slot, record);
    }

    if (frame->depth <= depth) {
        return;
    }

    if (frame->stack[frame->depth - 1].metricIndex == metricIndex) {
        frame->depth = depth;
        return;
    }

    for (std::uint32_t index = frame->depth; index > 0; --index) {
        if (frame->stack[index - 1].metricIndex == metricIndex) {
            frame->depth = index - 1;
            return;
        }
    }
}

ScopedMetricsContext::ScopedMetricsContext(PerformanceMetrics &metrics) noexcept
    : previous(threadContext().currentMetrics) {
    threadContext().currentMetrics = &metrics;
}

ScopedMetricsContext::~ScopedMetricsContext() {
    threadContext().currentMetrics = previous;
}

PerformanceMetrics *currentPerformanceMetrics() noexcept {
    return threadContext().currentMetrics;
}

PerformanceMetrics &defaultPerformanceMetrics() noexcept {
    static PerformanceMetrics *const metrics = []() noexcept {
        auto created = std::make_unique<PerformanceMetrics>();
        std::atexit(writeDefaultMetricsAtExit);
        return created.release();
    }();
    return *metrics;
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
