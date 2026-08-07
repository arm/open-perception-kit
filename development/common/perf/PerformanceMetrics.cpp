/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "perf/PerformanceMetrics.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <limits>
#include <memory>
#include <mutex>
#include <sys/syscall.h>
#include <thread>
#include <type_traits>
#include <unistd.h>

namespace pek::perf {

namespace {

constexpr std::uint32_t MaxThreadSlots = 128;
constexpr std::uint32_t MaxMetricsPerThread = 1024;
constexpr std::uint32_t MaxHistoryEventsPerChunk = 4096;
constexpr std::uint32_t MaxStackDepth = 64;
constexpr std::uint32_t InvalidLocalMetricIndex = std::numeric_limits<std::uint32_t>::max();

// A recorder keeps two related representations:
//
//  * aggregate metrics summarize every completed scope by its name and parent;
//  * optional history records every completed scope invocation as a SpanRecord.
//
// Each recording thread writes to its own PerformanceMetricsThreadSlot. This keeps the common
// recording path free from a recorder-wide lock. Snapshot collection later merges the per-thread
// aggregates into one hierarchy.

// One open scope in a ThreadFrame. metricIndex identifies the aggregate metric for the scope,
// while spanId identifies this particular invocation when history recording is enabled. Keeping
// both lets child scopes establish their aggregate parent and their historical parent cheaply.
struct StackEntry {
    std::uint32_t metricIndex = InvalidLocalMetricIndex;
    std::uint64_t spanId = PerformanceMetrics::InvalidSpanId;
};

// The nesting state for one (recorder, recording thread) pair. The frame identifies the thread's
// slot and stores the currently open scopes in call order. It is separate from the slot because
// the stack is transient recording state, whereas the slot also retains completed measurements.
struct ThreadFrame {
    std::uint32_t slotIndex = 0;
    std::array<StackEntry, MaxStackDepth> stack{};
    std::uint32_t depth = 0;
};

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
    return static_cast<std::uint64_t>(::syscall(SYS_gettid));
}

// Internal slot lookup needs a unique identifier. A process-wide counter assigns each thread a
// distinct token.
std::uint64_t currentThreadToken() noexcept {
    static std::atomic<std::uint64_t> nextToken{1};
    thread_local const std::uint64_t token = nextToken.fetch_add(1, std::memory_order_relaxed);
    return token;
}

// Names use fixed storage in the recording structures to avoid allocating on every scope. The
// return value records whether the caller's name had to be truncated.
bool copySpanName(PerformanceMetrics::SpanName &destination, std::string_view source) noexcept {
    const auto sourceLength = source.size();
    const auto copiedLength = std::min(sourceLength, PerformanceMetrics::MaxSpanNameLength);

    std::copy_n(source.begin(), copiedLength, destination.begin());
    destination[copiedLength] = '\0';

    return sourceLength > PerformanceMetrics::MaxSpanNameLength;
}

// Fixed names are always null-terminated by copySpanName, so comparison and export can expose them
// as string views without carrying the full array capacity.
std::string_view storedNameView(const PerformanceMetrics::SpanName &name) noexcept {
    return name.data();
}

bool namesEqual(const PerformanceMetrics::SpanName &left,
                const PerformanceMetrics::SpanName &right) noexcept {
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

// Historical spans are allocated in chunks only when history is enabled. Chunking allows history
// to grow without moving previously recorded spans and avoids one allocation per scope.
struct HistoryChunk {
    std::array<PerformanceMetrics::SpanRecord, MaxHistoryEventsPerChunk> records;
    std::size_t size = 0;
};

// Collection of metrics. Name, parentIndex, and depth identify the path. The counters summarize
// every completed invocation of that path. The owning thread is the only writer, but snapshots may
// read concurrently, so counters are atomic and sequence changes make readers retry instead of
// combining values from different updates.
struct PerformanceMetricsAtomicMetric {
    std::atomic<std::uint64_t> sequence{0};
    PerformanceMetrics::SpanName name{};
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

// All recorder-owned data associated with one recording thread. The slot owns that thread's
// nesting frame, aggregate hierarchy, diagnostic counters, and optional history.
struct PerformanceMetricsThreadSlot {
    std::atomic<bool> active{false};
    // Used only to recover this slot after a thread switches between recorders
    std::uint64_t threadToken = 0;
    // The Linux thread identifier written to exported SpanRecords.
    std::uint64_t threadId = 0;
    std::atomic<std::uint32_t> droppedMetrics{0};
    std::atomic<std::uint32_t> droppedSpans{0};
    std::atomic<std::uint32_t> droppedHistoryEvents{0};
    std::atomic<std::uint32_t> wrongThreadScopeCloses{0};
    std::atomic<std::uint32_t> metricCount{0};
    std::unique_ptr<ThreadFrame> frame;
    // Aggregate storage is fixed-size to keep recording allocation-free.
    std::array<PerformanceMetricsAtomicMetric, MaxMetricsPerThread> metrics;
    mutable std::mutex historyMutex;
    // The history is dynamically sized because its size can grow for long captures.
    std::vector<std::unique_ptr<HistoryChunk>> historyChunks;
};

// Complete owned state of one PerformanceMetrics recorder. Threads claim slots monotonically;
// slots are not recycled because their aggregate results and historical storage might be used by
// future snapshots. The state also owns recorder-wide options, unique span IDs, and automatic CSV
// configuration.
//
// Owning ThreadFrames (through the PerformanceMetricsThreadSlot) here is intentional: recorder
// destruction can release them even when a framework retains worker threads and therefore does not
// destroy those threads' thread-local storage objects.
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

} // namespace detail

namespace {

// Trivial thread-local navigation state, not measurement storage. state, instanceId, and frame
// cache the most recently used (recorder, thread) frame so repeated scopes avoid scanning slots.
// currentMetrics is the recorder temporarily selected by ScopedMetricsContext.
//
// The pointers are non-owning. PerformanceMetricsState owns frames through its thread slots; this
// ensures recorder destruction releases them even if a framework retains the worker thread and
// delays TLS cleanup until after leak reporting.
struct ThreadContext {
    const detail::PerformanceMetricsState *state = nullptr;
    std::uint64_t instanceId = 0;
    ThreadFrame *frame = nullptr;
    PerformanceMetrics *currentMetrics = nullptr;
};

static_assert(std::is_trivially_destructible_v<ThreadContext>,
              "ThreadContext must not require TLS destructor registration");

// Returns the calling thread's single context. It remains trivially destructible so accessing
// performance metrics does not add a dynamic TLS cleanup allocation.
ThreadContext &threadContext() noexcept {
    // Function-local storage preserves per-thread lazy initialization. Sonar warning S6018 applies
    // to global variables declared in headers, not to this local variable.
    thread_local ThreadContext context; // NOSONAR
    return context;
}

// Finds this thread's frame for a recorder. The last-used frame is returned directly on the common
// path. After switching recorders, the function scans that recorder's published slots, finds the
// one bearing this thread's token, and refreshes the cache.
ThreadFrame *findThreadFrame(const detail::PerformanceMetricsState &state) noexcept {
    auto &context = threadContext();
    if (context.state == &state && context.instanceId == state.instanceId &&
        context.frame != nullptr) {
        return context.frame;
    }

    const auto threadToken = currentThreadToken();
    const auto slotCount = std::min(state.nextSlot.load(std::memory_order_acquire), MaxThreadSlots);
    for (std::uint32_t index = 0; index < slotCount; ++index) {
        const auto &slot = state.slots[index];
        if (slot.active.load(std::memory_order_acquire) && slot.threadToken == threadToken &&
            slot.frame != nullptr) {
            context.state = &state;
            context.instanceId = state.instanceId;
            context.frame = slot.frame.get();
            return context.frame;
        }
    }

    return nullptr;
}

// Returns the existing frame for this (recorder, thread) pair or claims and initializes one state
// slot. The frame is allocated dynamically but owned by the state. active is published last so
// concurrent snapshots never observe a partially initialized slot.
ThreadFrame *acquireThreadFrame(detail::PerformanceMetricsState &state) {
    if (auto *frame = findThreadFrame(state)) {
        return frame;
    }

    const auto slotIndex = state.nextSlot.fetch_add(1, std::memory_order_acq_rel);
    if (slotIndex >= MaxThreadSlots) {
        state.threadSlotOverflow.store(true, std::memory_order_release);
        return nullptr;
    }

    auto &slot = state.slots[slotIndex];
    auto frame = std::make_unique<ThreadFrame>();
    frame->slotIndex = slotIndex;

    slot.threadToken = currentThreadToken();
    slot.threadId = currentThreadId();
    slot.droppedMetrics.store(0, std::memory_order_relaxed);
    slot.droppedSpans.store(0, std::memory_order_relaxed);
    slot.droppedHistoryEvents.store(0, std::memory_order_relaxed);
    slot.wrongThreadScopeCloses.store(0, std::memory_order_relaxed);
    slot.metricCount.store(0, std::memory_order_relaxed);
    slot.frame = std::move(frame);
    slot.active.store(true, std::memory_order_release);

    auto &context = threadContext();
    context.state = &state;
    context.instanceId = state.instanceId;
    context.frame = slot.frame.get();
    return context.frame;
}

// Locates or creates the aggregate metric identified by (parentIndex, name) in one thread slot.
// This key preserves hierarchy when the same name appears below different parents. Metric metadata
// is initialized before metricCount publishes the new entry to concurrent snapshot readers.
std::uint32_t ensureMetric(detail::PerformanceMetricsThreadSlot &slot,
                           const PerformanceMetrics::SpanName &name,
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

// Writers make sequence odd before changing a metric and even after finishing. Readers retry if
// they see an odd value or a change, giving them a coherent set of otherwise independent atomics.
void beginMetricWrite(detail::PerformanceMetricsAtomicMetric &metric) noexcept {
    metric.sequence.fetch_add(1, std::memory_order_acq_rel);
}

void endMetricWrite(detail::PerformanceMetricsAtomicMetric &metric) noexcept {
    metric.sequence.fetch_add(1, std::memory_order_release);
}

// Retains empty parent nodes in snapshots when they have a recorded descendant.
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

// Incorporates one completed scope duration into its thread-local aggregate node.
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

// Copies one aggregate node while its owner thread may still be updating it. The sequence check
// retries until every copied field belongs to the same completed write.
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

// Combines a thread-local metric with the matching path in the recorder-wide snapshot. parentId is
// already translated from the source slot's local index to the destination hierarchy's ID.
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

// Recursively collects a metric's parent first, then translates and merges the metric itself.
// mergedIds memoizes the local-index to snapshot-ID mapping for this source slot.
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

// Merges every published aggregate node from one thread slot into the destination snapshot.
void collectMetrics(std::vector<PerformanceMetrics::MetricRecord> &destination,
                    const detail::PerformanceMetricsThreadSlot &slot) {
    const auto metricCount =
        std::min(slot.metricCount.load(std::memory_order_acquire), MaxMetricsPerThread);
    std::vector<std::uint64_t> mergedIds(metricCount, PerformanceMetrics::InvalidMetricId);

    for (std::uint32_t index = 0; index < metricCount; ++index) {
        collectMetric(destination, slot, index, metricCount, mergedIds);
    }
}

// Appends one completed historical span. History is the only recording storage that grows without
// a fixed event limit; allocation failures are reported as dropped events instead of escaping from
// the noexcept scope-closing path.
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

// Copies the completed historical spans from one slot while excluding unused chunk capacity. The
// same mutex protects chunk growth and snapshot copying.
void collectHistoryEvents(std::vector<PerformanceMetrics::SpanRecord> &destination,
                          const detail::PerformanceMetricsThreadSlot &slot) {
    std::scoped_lock lock(slot.historyMutex);
    for (const auto &chunk : slot.historyChunks) {
        destination.insert(
            destination.end(), chunk->records.begin(), chunk->records.begin() + chunk->size);
    }
}

} // namespace

// Scope owns the obligation to finish one recording. Move operations transfer that obligation and
// clear the source so exactly one Scope reports the duration and unwinds the nesting frame.
PerformanceMetrics::Scope::Scope(const Recording &recording) noexcept : recording(recording) {}

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

// Completes the recording once. Clearing metrics also makes explicit close, destruction, and moved
// scopes safely idempotent.
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

// Constructs the private implementation state through the incomplete-type-aware pointer declared
// in the public header.
detail::PerformanceMetricsStatePtr PerformanceMetrics::createState() {
    return std::make_unique<detail::PerformanceMetricsState>(nextInstanceId());
}

PerformanceMetrics::PerformanceMetrics() : state(createState()) {}

// Automatic CSV export runs while the recorder and all state-owned history are still alive. State
// destruction then releases every thread frame, including frames belonging to retained workers.
PerformanceMetrics::~PerformanceMetrics() {
    writeAutoCsv();
}

// Starts one timed scope on the calling thread. It resolves the thread frame, finds the aggregate
// hierarchy node, optionally assigns historical span IDs, pushes the node on the nesting stack,
// and returns an RAII Scope containing everything needed to finish the recording.
//
// Failures return an inactive Scope because instrumentation must not disrupt the measured work.
PerformanceMetrics::Scope PerformanceMetrics::scope(std::string_view name) noexcept {
    if (!state->enabled.load(std::memory_order_relaxed)) {
        return {};
    }

    try {
        auto *frame = acquireThreadFrame(*state);
        if (frame == nullptr) {
            return {};
        }

        auto &slot = state->slots[frame->slotIndex];
        if (frame->depth >= MaxStackDepth) {
            slot.droppedSpans.fetch_add(1, std::memory_order_relaxed);
            return {};
        }

        SpanName storedName{};
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

// Recorder options are atomic because instrumentation and snapshot/control code may access them
// from different threads. Trace is retained as an alias for the historical-span option.
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

// The auto-export path is less frequently accessed and dynamically sized, so a mutex protects it
// instead of placing it on the lock-free recording path.
void PerformanceMetrics::setAutoCsvExportPath(std::string_view path) {
    std::scoped_lock lock(state->autoCsvMutex);
    state->autoCsvPath.assign(path.begin(), path.end());
}

std::string PerformanceMetrics::autoCsvExportPath() const {
    std::scoped_lock lock(state->autoCsvMutex);
    return state->autoCsvPath;
}

// Exports individual historical spans rather than aggregate MetricRecords. History from all thread
// slots is copied, ordered by start time, and CSV-escaped before writing.
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

// Best-effort destructor helper: an empty path disables export, and failures are reported without
// allowing exceptions to escape destruction or process shutdown.
void PerformanceMetrics::writeAutoCsv() const noexcept {
    try {
        const auto path = autoCsvExportPath();
        if (!path.empty() && !writeCsv(path)) {
            std::fputs("Performance metrics CSV export failed.\n", stderr);
        }
    } catch (...) {
        std::fputs("Performance metrics CSV export failed with an unexpected error.\n", stderr);
    }
}

// Builds the inexpensive summary view. Per-thread hierarchies are merged by parent path and name,
// and diagnostic counters are accumulated, but individual historical spans are not copied.
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

// Builds the full view by extending the aggregate snapshot with every completed historical span,
// ordered into a recorder-wide timeline.
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

// Finishes a Scope: verify it is closing on its recording thread, update aggregate duration,
// append an optional historical SpanRecord, and remove the scope from that thread's nesting stack.
// The final search also recovers conservatively if scopes are closed out of strict LIFO order.
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
    auto *frame = findThreadFrame(*state);
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

// Temporarily binds a recorder to this thread for code using enterCurrentBlock(). Nested bindings
// work because construction saves and destruction restores the previous pointer.
ScopedMetricsContext::ScopedMetricsContext(PerformanceMetrics &metrics) noexcept
    : previous(threadContext().currentMetrics) {
    threadContext().currentMetrics = &metrics;
}

ScopedMetricsContext::~ScopedMetricsContext() {
    threadContext().currentMetrics = previous;
}

// Returns only the explicitly thread-bound recorder; it does not fall back to the global recorder.
PerformanceMetrics *currentPerformanceMetrics() noexcept {
    return threadContext().currentMetrics;
}

// Lazily constructs the process-wide recorder used by the convenience API. Normal static
// destruction performs optional CSV export and releases the recorder-owned frames.
PerformanceMetrics &defaultPerformanceMetrics() noexcept {
    // Function-local storage is required for lazy initialization and deterministic destruction.
    // S6018 applies to global variables declared in headers, not to this local singleton.
    static PerformanceMetrics metrics; // NOSONAR
    return metrics;
}

// Convenience entry point for instrumentation that explicitly targets the process-wide recorder.
PerformanceMetrics::Scope enterBlock(std::string_view name) noexcept {
    return defaultPerformanceMetrics().scope(name);
}

// Convenience entry point for code that should record only when its thread has an explicit
// ScopedMetricsContext binding.
PerformanceMetrics::Scope enterCurrentBlock(std::string_view name) noexcept {
    auto *metrics = currentPerformanceMetrics();
    if (metrics == nullptr) {
        return {};
    }

    return metrics->scope(name);
}

} // namespace pek::perf
