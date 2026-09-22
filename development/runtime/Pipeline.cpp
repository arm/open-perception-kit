/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "runtime/Pipeline.h"

#include "Log.h"
#include "gst/FrameResultsMeta.h"
#include "opk/Base64.h"
#include "opk/FrameResults.h"
#include "opk/PipelinePreset.h"

#include <fmt/core.h>

#include <gst/gst.h>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <mutex>
#include <optional>
#include <source_location>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace opk::runtime {
namespace {

constexpr auto kProgressUpdateInterval = std::chrono::milliseconds(500);
constexpr auto kLoopTargetDiscoveryGracePeriod = std::chrono::seconds(5);

void setDefaultOnnxRuntimeLogSeverity() {
    const char *configured = std::getenv("ORT_LOG_SEVERITY_LEVEL");
    if (configured && configured[0] != '\0') {
        return;
    }

#ifdef _WIN32
    _putenv_s("ORT_LOG_SEVERITY_LEVEL", "3");
#else
    setenv("ORT_LOG_SEVERITY_LEVEL", "3", 0);
#endif
}

// GStreamer initialization is process-global. The Runtime initializes it on
// demand, but does not deinitialize it behind embedders' backs.
class GstreamerLifecycle {
  public:
    GstreamerLifecycle() {
        setDefaultOnnxRuntimeLogSeverity();
        gst_init(nullptr, nullptr);
    }

    GstreamerLifecycle(const GstreamerLifecycle &) = delete;
    GstreamerLifecycle &operator=(const GstreamerLifecycle &) = delete;
    GstreamerLifecycle(GstreamerLifecycle &&) = delete;
    GstreamerLifecycle &operator=(GstreamerLifecycle &&) = delete;

    ~GstreamerLifecycle() = default;
};

void ensureGstInitialized() {
    static GstreamerLifecycle lifecycle;
    (void)lifecycle;
}

Error makeError(ErrorFlag flag,
                const std::string &message,
                std::source_location loc = std::source_location::current()) {
    return Error(flag, message, loc);
}

std::string gstErrorMessage(GError *error, const gchar *debugInfo) {
    std::string message = error && error->message ? error->message : "unknown GStreamer error";
    if (debugInfo && debugInfo[0] != '\0') {
        message += fmt::format(" ({})", debugInfo);
    }
    return message;
}

std::string qosValue(guint64 value) {
    return value == G_MAXUINT64 ? "unknown" : fmt::format("{}", value);
}

void logQosMessage(GstMessage *message) {
    gboolean live = FALSE;
    guint64 runningTime = GST_CLOCK_TIME_NONE;
    guint64 streamTime = GST_CLOCK_TIME_NONE;
    guint64 timestamp = GST_CLOCK_TIME_NONE;
    guint64 duration = GST_CLOCK_TIME_NONE;
    gst_message_parse_qos(message, &live, &runningTime, &streamTime, &timestamp, &duration);

    gint64 jitter = 0;
    gdouble proportion = 1.0;
    gint quality = 1'000'000;
    gst_message_parse_qos_values(message, &jitter, &proportion, &quality);

    GstFormat format = GST_FORMAT_UNDEFINED;
    guint64 processed = G_MAXUINT64;
    guint64 dropped = G_MAXUINT64;
    gst_message_parse_qos_stats(message, &format, &processed, &dropped);

    const auto *source = GST_MESSAGE_SRC(message);
    const char *sourceName = source ? GST_OBJECT_NAME(source) : "unknown";
    const char *formatName = gst_format_get_name(format);
    opk::log::debug("GStreamer QoS: source={}, live={}, running_time_ns={}, stream_time_ns={}, "
                    "timestamp_ns={}, duration_ns={}, jitter_ns={}, proportion={}, quality={}, "
                    "format={}, processed={}, dropped={}",
                    sourceName,
                    live != FALSE,
                    qosValue(runningTime),
                    qosValue(streamTime),
                    qosValue(timestamp),
                    qosValue(duration),
                    jitter,
                    proportion,
                    quality,
                    formatName ? formatName : "unknown",
                    qosValue(processed),
                    qosValue(dropped));
}

std::string serializeFrameResultsJson(const std::vector<std::uint8_t> &packet) {
    nlohmann::json wrapper;
    wrapper["frame_results_encoding"] = "perception-frame-results+base64";
    wrapper["frame_results_packet_b64"] = opk::base64Encode(packet);
    return wrapper.dump();
}

Error pipelinePresetError(const opk::Error &error) {
    ErrorFlag flag = ErrorFlag::InternalError;
    switch (error.flag) {
    case opk::ErrorFlag::FileNotFound:
        flag = ErrorFlag::FileNotFound;
        break;
    case opk::ErrorFlag::ParseError:
        flag = ErrorFlag::ParseError;
        break;
    case opk::ErrorFlag::InvalidData:
        flag = ErrorFlag::InvalidPipeline;
        break;
    case opk::ErrorFlag::FileOperationError:
        flag = ErrorFlag::RuntimeError;
        break;
    default:
        break;
    }
    return makeError(flag, error.info);
}

// Consume a GStreamer iterator just far enough to know whether it contains at
// least one item. Used to detect terminal elements without exposing Gst types.
bool iteratorHasAny(GstIterator *iterator) {
    if (!iterator) {
        return false;
    }

    bool found = false;
    bool done = false;
    GValue item = G_VALUE_INIT;

    while (!done) {
        switch (gst_iterator_next(iterator, &item)) {
        case GST_ITERATOR_OK:
            found = true;
            g_value_reset(&item);
            done = true;
            break;
        case GST_ITERATOR_RESYNC:
            gst_iterator_resync(iterator);
            break;
        case GST_ITERATOR_DONE:
        case GST_ITERATOR_ERROR:
            done = true;
            break;
        }
    }

    gst_iterator_free(iterator);
    return found;
}

} // namespace

class Pipeline::Impl {
  public:
    struct ProbeHandle {
        GstPad *pad = nullptr;
        gulong id = 0;
    };

    enum class TerminalState { None, Eos, Error, Stopped };

    ~Impl() {
        clearPipeline();
    }

    // Parse a GStreamer launch string and install default perception probes.
    Result<void> loadFromString(const std::string &description) {
        ensureGstInitialized();
        clearPipeline();

        auto expandedDescription = opk::expandPipelineDescription(description);
        if (!expandedDescription) {
            return tl::make_unexpected(pipelinePresetError(expandedDescription.error()));
        }

        GError *parseError = nullptr;
        GstElement *parsed = gst_parse_launch(expandedDescription->c_str(), &parseError);
        if (!parsed) {
            std::string message = parseError && parseError->message ? parseError->message
                                                                    : "failed to parse pipeline";
            if (parseError) {
                g_error_free(parseError);
            }
            return tl::unexpected(makeError(ErrorFlag::InvalidPipeline, message));
        }

        if (parseError) {
            std::string message = parseError->message ? parseError->message
                                                      : "pipeline parsed with recoverable errors";
            g_error_free(parseError);
            gst_object_unref(parsed);
            return tl::unexpected(makeError(ErrorFlag::InvalidPipeline, message));
        }

        pipeline = parsed;
        installDefaultPerceptionProbes();
        return {};
    }

    Result<void> loadFromJsonFile(const std::string &path) {
        auto preset = opk::PipelinePreset::fromFile(path);
        if (!preset) {
            return tl::make_unexpected(pipelinePresetError(preset.error()));
        }

        return loadFromString(preset->pipeline);
    }

    // start(), pause(), and stop() all use this small shared state helper.
    Result<void> setState(GstState state, const char *stateName) {
        if (!pipeline) {
            return tl::unexpected(
                makeError(ErrorFlag::InvalidPipeline, "No pipeline has been loaded"));
        }

        const GstStateChangeReturn ret = gst_element_set_state(pipeline, state);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            return tl::unexpected(
                makeError(ErrorFlag::RuntimeError,
                          fmt::format("Failed to set pipeline state to {}", stateName)));
        }

        return {};
    }

    Result<void> start(const Pipeline::StartOptions &options) {
        if (!pipeline) {
            return tl::unexpected(
                makeError(ErrorFlag::InvalidPipeline, "No pipeline has been loaded"));
        }

        setLogLevel(options.logLevel);
        (void)setLogTargetState(LogTarget::Stdout, options.logToStdout);
        (void)setLogTargetState(LogTarget::Stderr, options.logToStderr);
        (void)setLogTargetState(LogTarget::File, options.logToFile);

        auto watcherResult = startBusWatcher(options.loop);
        if (!watcherResult) {
            return watcherResult;
        }

        const GstStateChangeReturn stateChange = gst_element_set_state(pipeline, GST_STATE_PLAYING);
        if (stateChange == GST_STATE_CHANGE_FAILURE) {
            stopBusWatcher();
            return tl::make_unexpected(
                makeError(ErrorFlag::RuntimeError, "Failed to set pipeline state to PLAYING"));
        }

        // An asynchronous state change is completed by GST_MESSAGE_ASYNC_DONE,
        // where the bus thread installs the initial segment seek. Pipelines that
        // reach PLAYING synchronously do not post that message, so configure
        // their segment here.
        if (options.loop && stateChange == GST_STATE_CHANGE_SUCCESS) {
            auto loopResult = restartLoopingSegment(pipeline, true, true);
            // Some dynamic pipelines expose their seekable pads shortly after
            // the state call. The bus thread retries until playback is ready and
            // turns a failure into a terminal error if EOS arrives first.
            (void)loopResult;
        }

        return {};
    }

    Result<void> stop() {
        stopBusWatcher();
        return setState(GST_STATE_NULL, "NULL");
    }

    Result<void> wait() {
        Error error;
        bool hasError = false;

        {
            std::unique_lock lock(lifecycleMutex);
            if (!pipeline) {
                return tl::unexpected(
                    makeError(ErrorFlag::InvalidPipeline, "No pipeline has been loaded"));
            }
            if (!busThread.joinable() && terminalState == TerminalState::None) {
                return tl::unexpected(
                    makeError(ErrorFlag::InvalidPipeline,
                              "Pipeline has not been started; call start() before wait()"));
            }

            lifecycleCv.wait(lock, [this]() { return terminalState != TerminalState::None; });
            hasError = terminalState == TerminalState::Error;
            if (hasError) {
                error = terminalError;
            }
        }

        joinBusWatcherIfNotCurrent();

        if (hasError) {
            return tl::unexpected(std::move(error));
        }
        return {};
    }

    void onFrameResults(Pipeline::FrameResultsCallback callback) {
        std::lock_guard lock(callbackMutex);
        frameResultsCallback = std::move(callback);
    }

    void onFrameResultsPacket(Pipeline::FrameResultsPacketCallback callback) {
        std::lock_guard lock(callbackMutex);
        frameResultsPacketCallback = std::move(callback);
    }

    void onProgress(Pipeline::ProgressCallback callback) {
        std::lock_guard lock(callbackMutex);
        progressCallback = std::move(callback);
    }

    void onError(Pipeline::ErrorCallback callback) {
        std::lock_guard lock(callbackMutex);
        errorCallback = std::move(callback);
    }

    void onEos(Pipeline::EosCallback callback) {
        std::lock_guard lock(callbackMutex);
        eosCallback = std::move(callback);
    }

    // Manual hook for users who want perception at a specific named element
    // instead of the automatically detected terminal sinks.
    Result<void> attachFrameResultsProbe(const std::string &elementName,
                                         const std::string &padName) {
        if (!pipeline) {
            return tl::unexpected(
                makeError(ErrorFlag::InvalidPipeline, "No pipeline has been loaded"));
        }
        if (!GST_IS_BIN(pipeline)) {
            return tl::unexpected(
                makeError(ErrorFlag::InvalidPipeline, "Loaded pipeline is not a GStreamer bin"));
        }

        GstElement *element = gst_bin_get_by_name(GST_BIN(pipeline), elementName.c_str());
        if (!element) {
            return tl::unexpected(
                makeError(ErrorFlag::InvalidPipeline,
                          fmt::format("Element '{}' was not found in the pipeline", elementName)));
        }

        GstPad *pad = gst_element_get_static_pad(element, padName.c_str());
        gst_object_unref(element);
        if (!pad) {
            return tl::unexpected(makeError(
                ErrorFlag::InvalidPipeline,
                fmt::format("Element '{}' does not have a static '{}' pad", elementName, padName)));
        }

        addProbeForPad(pad);
        gst_object_unref(pad);
        return {};
    }

    bool loaded() const noexcept {
        return pipeline != nullptr;
    }

  private:
    GstElement *pipeline = nullptr;
    std::vector<ProbeHandle> probes;
    mutable std::mutex callbackMutex;
    Pipeline::FrameResultsCallback frameResultsCallback;
    Pipeline::FrameResultsPacketCallback frameResultsPacketCallback;
    Pipeline::ProgressCallback progressCallback;
    Pipeline::ErrorCallback errorCallback;
    Pipeline::EosCallback eosCallback;

    std::mutex lifecycleMutex;
    std::mutex loopOperationMutex;
    std::condition_variable lifecycleCv;
    std::thread busThread;
    bool busStopRequested = false;
    bool loopRequested = false;
    bool loopSegmentStarted = false;
    TerminalState terminalState = TerminalState::None;
    Error terminalError;

    // Release the current pipeline and any probes before loading a new one.
    void clearPipeline() {
        stopBusWatcher();
        removeProbes();
        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
            pipeline = nullptr;
        }
    }

    void removeProbes() {
        for (const auto &probe : probes) {
            if (probe.pad && probe.id != 0) {
                gst_pad_remove_probe(probe.pad, probe.id);
            }
            if (probe.pad) {
                gst_object_unref(probe.pad);
            }
        }
        probes.clear();
    }

    Result<void> startBusWatcher(bool loop) {
        std::thread oldThread;
        {
            std::unique_lock lock(lifecycleMutex);
            if (busThread.joinable() && terminalState == TerminalState::None) {
                return {};
            }
            if (busThread.joinable()) {
                oldThread = std::move(busThread);
            }
        }

        if (oldThread.joinable()) {
            oldThread.join();
        }

        auto *watchedPipeline = GST_ELEMENT(gst_object_ref(pipeline));
        {
            std::lock_guard lock(lifecycleMutex);
            busStopRequested = false;
            loopRequested = loop;
            loopSegmentStarted = false;
            terminalState = TerminalState::None;
            terminalError = Error();
            busThread = std::thread(&Pipeline::Impl::busLoop, this, watchedPipeline);
        }
        return {};
    }

    void stopBusWatcher() {
        std::thread threadToJoin;
        {
            std::lock_guard lock(lifecycleMutex);
            busStopRequested = true;
            if (terminalState == TerminalState::None) {
                terminalState = TerminalState::Stopped;
                lifecycleCv.notify_all();
            }

            if (busThread.joinable() && busThread.get_id() != std::this_thread::get_id()) {
                threadToJoin = std::move(busThread);
            }
        }

        if (threadToJoin.joinable()) {
            threadToJoin.join();
        }
    }

    void joinBusWatcherIfNotCurrent() {
        std::thread threadToJoin;
        {
            std::lock_guard lock(lifecycleMutex);
            if (busThread.joinable() && busThread.get_id() != std::this_thread::get_id()) {
                threadToJoin = std::move(busThread);
            }
        }

        if (threadToJoin.joinable()) {
            threadToJoin.join();
        }
    }

    bool shouldStopBusWatcher() {
        std::lock_guard lock(lifecycleMutex);
        return busStopRequested;
    }

    bool loopingIsRequested() {
        std::lock_guard lock(lifecycleMutex);
        return loopRequested;
    }

    bool loopingSegmentHasStarted() {
        std::lock_guard lock(lifecycleMutex);
        return loopSegmentStarted;
    }

    bool pipelineIsPlaying(GstElement *watchedPipeline) const {
        GstState state = GST_STATE_VOID_PENDING;
        GstState pending = GST_STATE_VOID_PENDING;
        const GstStateChangeReturn result =
            gst_element_get_state(watchedPipeline, &state, &pending, 0);
        return result != GST_STATE_CHANGE_FAILURE && state == GST_STATE_PLAYING;
    }

    void markLoopingSegmentStarted() {
        std::lock_guard lock(lifecycleMutex);
        loopSegmentStarted = true;
    }

    struct LoopTarget {
        GstPad *upstreamPad = nullptr;
        gint64 start = 0;
        gint64 end = -1;
    };

    void appendLoopTarget(GstPad *sinkPad, std::vector<LoopTarget> &targets) const {
        if (!sinkPad || !gst_pad_is_linked(sinkPad)) {
            return;
        }

        GstPad *upstreamPad = gst_pad_get_peer(sinkPad);
        if (!upstreamPad) {
            return;
        }

        GstQuery *seekingQuery = gst_query_new_seeking(GST_FORMAT_TIME);
        const gboolean querySucceeded = gst_pad_query(upstreamPad, seekingQuery);

        GstFormat format = GST_FORMAT_UNDEFINED;
        gboolean seekable = FALSE;
        gint64 seekStart = 0;
        gint64 seekEnd = -1;
        if (querySucceeded) {
            gst_query_parse_seeking(seekingQuery, &format, &seekable, &seekStart, &seekEnd);
        }
        gst_query_unref(seekingQuery);

        if (!querySucceeded || format != GST_FORMAT_TIME || seekable == FALSE) {
            gst_object_unref(upstreamPad);
            return;
        }

        if (seekEnd <= seekStart) {
            GstQuery *durationQuery = gst_query_new_duration(GST_FORMAT_TIME);
            const gboolean durationSucceeded = gst_pad_query(upstreamPad, durationQuery);
            gint64 duration = -1;
            if (durationSucceeded) {
                gst_query_parse_duration(durationQuery, &format, &duration);
            }
            gst_query_unref(durationQuery);
            if (!durationSucceeded || format != GST_FORMAT_TIME || duration <= seekStart) {
                gst_object_unref(upstreamPad);
                return;
            }
            seekEnd = duration;
        }

        targets.push_back(LoopTarget{
            .upstreamPad = upstreamPad,
            .start = seekStart,
            .end = seekEnd,
        });
    }

    void appendLoopTargetsForElement(GstElement *element, std::vector<LoopTarget> &targets) const {
        if (!element || iteratorHasAny(gst_element_iterate_src_pads(element))) {
            return;
        }

        GstIterator *sinkPads = gst_element_iterate_sink_pads(element);
        if (!sinkPads) {
            return;
        }

        bool done = false;
        GValue item = G_VALUE_INIT;
        while (!done) {
            switch (gst_iterator_next(sinkPads, &item)) {
            case GST_ITERATOR_OK:
                appendLoopTarget(GST_PAD(g_value_get_object(&item)), targets);
                g_value_reset(&item);
                break;
            case GST_ITERATOR_RESYNC:
                gst_iterator_resync(sinkPads);
                break;
            case GST_ITERATOR_DONE:
            case GST_ITERATOR_ERROR:
                done = true;
                break;
            }
        }
        gst_iterator_free(sinkPads);
    }

    std::vector<LoopTarget> discoverLoopTargets(GstElement *watchedPipeline) const {
        std::vector<LoopTarget> targets;
        if (!GST_IS_BIN(watchedPipeline)) {
            appendLoopTargetsForElement(watchedPipeline, targets);
            return targets;
        }

        // Inspect only the top-level terminal elements. In particular, this
        // avoids seeking opksink's internal live silence source while still
        // following its externally linked video sink pad upstream.
        GstIterator *elements = gst_bin_iterate_elements(GST_BIN(watchedPipeline));
        if (!elements) {
            return targets;
        }

        bool done = false;
        GValue item = G_VALUE_INIT;
        while (!done) {
            switch (gst_iterator_next(elements, &item)) {
            case GST_ITERATOR_OK:
                appendLoopTargetsForElement(GST_ELEMENT(g_value_get_object(&item)), targets);
                g_value_reset(&item);
                break;
            case GST_ITERATOR_RESYNC:
                gst_iterator_resync(elements);
                break;
            case GST_ITERATOR_DONE:
            case GST_ITERATOR_ERROR:
                done = true;
                break;
            }
        }
        gst_iterator_free(elements);
        return targets;
    }

    Result<void>
    restartLoopingSegment(GstElement *watchedPipeline, bool flush, bool initialSegment = false) {
        std::lock_guard operationLock(loopOperationMutex);
        if (initialSegment && loopingSegmentHasStarted()) {
            return {};
        }

        auto targets = discoverLoopTargets(watchedPipeline);
        if (targets.empty()) {
            return tl::make_unexpected(makeError(
                ErrorFlag::NotSupported,
                "Looping requires a finite, seekable terminal media branch in the time domain"));
        }

        GstSeekFlags flags = GST_SEEK_FLAG_SEGMENT;
        if (flush) {
            flags = static_cast<GstSeekFlags>(flags | GST_SEEK_FLAG_FLUSH);
        }

        bool seekSucceeded = false;
        for (const auto &target : targets) {
            if (!seekSucceeded) {
                GstEvent *seekEvent = gst_event_new_seek(1.0,
                                                         GST_FORMAT_TIME,
                                                         flags,
                                                         GST_SEEK_TYPE_SET,
                                                         target.start,
                                                         GST_SEEK_TYPE_SET,
                                                         target.end);
                // Applications send an upstream seek to the source pad on the
                // selected terminal branch. This keeps unrelated live branches
                // out of the seek while allowing the pad's event handler to
                // propagate it to the actual source or demuxer.
                seekSucceeded = gst_pad_send_event(target.upstreamPad, seekEvent);
            }
            gst_object_unref(target.upstreamPad);
        }

        if (!seekSucceeded) {
            return tl::make_unexpected(makeError(ErrorFlag::RuntimeError,
                                                 "Failed to seek pipeline back to the loop start"));
        }

        markLoopingSegmentStarted();
        return {};
    }

    void busLoop(GstElement *watchedPipeline) { // NOSONAR: bus message handling is intentionally
                                                // centralized.
        GstBus *bus = gst_element_get_bus(watchedPipeline);
        if (!bus) {
            gst_object_unref(watchedPipeline);
            finishWithError(makeError(ErrorFlag::RuntimeError,
                                      "Loaded element does not expose a GStreamer bus"));
            return;
        }

        auto nextProgressUpdate = std::chrono::steady_clock::now();
        std::optional<std::chrono::steady_clock::time_point> loopTargetDiscoveryDeadline;
        auto ensureLoopTargetDiscoveryDeadline = [&]() {
            if (!loopTargetDiscoveryDeadline && pipelineIsPlaying(watchedPipeline)) {
                loopTargetDiscoveryDeadline =
                    std::chrono::steady_clock::now() + kLoopTargetDiscoveryGracePeriod;
            }
        };
        auto handleInitialLoopResult = [&](const Result<void> &loopResult) {
            if (loopResult) {
                return false;
            }

            const auto &error = loopResult.error();
            if (error.flag != ErrorFlag::NotSupported) {
                finishWithError(error);
                return true;
            }

            if (loopTargetDiscoveryDeadline &&
                std::chrono::steady_clock::now() >= *loopTargetDiscoveryDeadline) {
                finishWithError(error);
                return true;
            }
            return false;
        };
        auto tryInitialLoopSegment = [&]() {
            ensureLoopTargetDiscoveryDeadline();
            return handleInitialLoopResult(restartLoopingSegment(watchedPipeline, true, true));
        };
        auto shouldRetryInitialLoopAfterStateChange = [&](GstMessage *message) {
            if (GST_MESSAGE_TYPE(message) != GST_MESSAGE_STATE_CHANGED ||
                GST_MESSAGE_SRC(message) != GST_OBJECT(watchedPipeline) || !loopingIsRequested() ||
                loopingSegmentHasStarted()) {
                return false;
            }

            GstState oldState = GST_STATE_VOID_PENDING;
            GstState newState = GST_STATE_VOID_PENDING;
            GstState pendingState = GST_STATE_VOID_PENDING;
            gst_message_parse_state_changed(message, &oldState, &newState, &pendingState);
            return newState == GST_STATE_PLAYING;
        };

        while (!shouldStopBusWatcher()) { // NOSONAR: bus watcher handles terminal events here.
            const auto now = std::chrono::steady_clock::now();
            if (now >= nextProgressUpdate) {
                emitProgress(watchedPipeline);
                nextProgressUpdate = now + kProgressUpdateInterval;
            }

            // Dynamic pads and duration information can appear after the
            // initial state transition, especially when a sink also owns a
            // live branch. Retry briefly after PLAYING before failing.
            if (loopingIsRequested() && !loopingSegmentHasStarted() && tryInitialLoopSegment())
                break;

            GstMessage *message = gst_bus_timed_pop_filtered(
                bus,
                100 * GST_MSECOND,
                static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_QOS |
                                            GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_SEGMENT_DONE |
                                            GST_MESSAGE_STATE_CHANGED));

            if (!message) {
                continue;
            }

            if (shouldStopBusWatcher()) {
                gst_message_unref(message);
                break;
            }

            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
                gst_message_unref(message);
                if (loopingIsRequested()) {
                    auto loopResult = restartLoopingSegment(watchedPipeline, true);
                    if (!loopResult) { // NOSONAR: keep EOS-loop failure beside EOS handling.
                        finishWithError(loopResult.error());
                        break;
                    }
                    continue;
                }
                finishWithEos();
                break;
            }

            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
                GError *error = nullptr;
                gchar *debugInfo = nullptr;
                gst_message_parse_error(message, &error, &debugInfo);

                auto runtimeError =
                    makeError(ErrorFlag::RuntimeError, gstErrorMessage(error, debugInfo));

                if (error) {
                    g_error_free(error);
                }
                if (debugInfo) {
                    g_free(debugInfo);
                }
                gst_message_unref(message);
                finishWithError(runtimeError);
                break;
            }

            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_QOS) {
                logQosMessage(message);
            }

            if (shouldRetryInitialLoopAfterStateChange(message) && tryInitialLoopSegment()) {
                gst_message_unref(message);
                break;
            }

            // Dynamic pads and duration queries can lag behind the state
            // transition. The timed loop keeps retrying for a bounded grace
            // period, then reports NotSupported.
            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ASYNC_DONE && loopingIsRequested() &&
                !loopingSegmentHasStarted() && tryInitialLoopSegment()) {
                gst_message_unref(message);
                break;
            }

            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_SEGMENT_DONE && loopingIsRequested()) {
                auto loopResult = restartLoopingSegment(watchedPipeline, false);
                if (!loopResult) {
                    gst_message_unref(message);
                    finishWithError(loopResult.error());
                    break;
                }
            }

            gst_message_unref(message);
        }

        gst_object_unref(bus);
        gst_object_unref(watchedPipeline);
    }

    void emitProgress(GstElement *watchedPipeline) {
        Pipeline::ProgressCallback callback;
        {
            std::lock_guard lock(callbackMutex);
            callback = progressCallback;
        }
        if (!callback) {
            return;
        }

        Pipeline::Progress progress;
        GstFormat format = GST_FORMAT_TIME;
        gint64 position = 0;
        if (gst_element_query_position(watchedPipeline, format, &position) && position >= 0) {
            progress.hasPosition = true;
            progress.positionNs = position;
        }

        format = GST_FORMAT_TIME;
        gint64 duration = 0;
        if (gst_element_query_duration(watchedPipeline, format, &duration) && duration > 0) {
            progress.hasDuration = true;
            progress.durationNs = duration;
        }

        callback(progress);
    }

    void finishWithEos() {
        bool shouldEmit = false;
        {
            std::lock_guard lock(lifecycleMutex);
            if (!busStopRequested && terminalState == TerminalState::None) {
                terminalState = TerminalState::Eos;
                shouldEmit = true;
                lifecycleCv.notify_all();
            }
        }

        if (shouldEmit) {
            emitEos();
        }
    }

    void finishWithError(const Error &error) {
        bool shouldEmit = false;
        {
            std::lock_guard lock(lifecycleMutex);
            if (!busStopRequested && terminalState == TerminalState::None) {
                terminalState = TerminalState::Error;
                terminalError = error;
                shouldEmit = true;
                lifecycleCv.notify_all();
            }
        }

        if (shouldEmit) {
            emitError(error);
        }
    }

    // By default, observe final buffers at terminal elements. For a normal
    // linear pipeline, this means the callback sees the final accumulated result.
    void installDefaultPerceptionProbes() {
        if (!pipeline) {
            return;
        }

        if (GST_IS_BIN(pipeline)) {
            installDefaultPerceptionProbesForBin(GST_BIN(pipeline));
            return;
        }

        installDefaultPerceptionProbeForElement(pipeline);
    }

    void installDefaultPerceptionProbesForBin(GstBin *bin) {
        GstIterator *iterator = gst_bin_iterate_recurse(bin);
        if (!iterator) {
            return;
        }

        bool done = false;
        GValue item = G_VALUE_INIT;
        while (!done) {
            switch (gst_iterator_next(iterator, &item)) {
            case GST_ITERATOR_OK: {
                auto *element = GST_ELEMENT(g_value_get_object(&item));
                installDefaultPerceptionProbeForElement(element);
                g_value_reset(&item);
                break;
            }
            case GST_ITERATOR_RESYNC:
                gst_iterator_resync(iterator);
                break;
            case GST_ITERATOR_DONE:
            case GST_ITERATOR_ERROR:
                done = true;
                break;
            }
        }

        gst_iterator_free(iterator);
    }

    // Treat elements with sink pads and no source pads as terminal sinks.
    void installDefaultPerceptionProbeForElement(GstElement *element) {
        if (!element) {
            return;
        }

        if (iteratorHasAny(gst_element_iterate_src_pads(element))) {
            return;
        }

        GstIterator *sinkPads = gst_element_iterate_sink_pads(element);
        if (!sinkPads) {
            return;
        }

        bool done = false;
        GValue item = G_VALUE_INIT;
        while (!done) {
            switch (gst_iterator_next(sinkPads, &item)) {
            case GST_ITERATOR_OK: {
                auto *pad = GST_PAD(g_value_get_object(&item));
                addProbeForPad(pad);
                g_value_reset(&item);
                break;
            }
            case GST_ITERATOR_RESYNC:
                gst_iterator_resync(sinkPads);
                break;
            case GST_ITERATOR_DONE:
            case GST_ITERATOR_ERROR:
                done = true;
                break;
            }
        }

        gst_iterator_free(sinkPads);
    }

    // Keep a ref to every probed pad so the probe can be removed safely later.
    void addProbeForPad(GstPad *pad) {
        if (!pad) {
            return;
        }

        const gulong id = gst_pad_add_probe(
            pad, GST_PAD_PROBE_TYPE_BUFFER, &Pipeline::Impl::perceptionProbe, this, nullptr);
        if (id == 0) {
            return;
        }

        probes.push_back(ProbeHandle{GST_PAD(gst_object_ref(pad)), id});
    }

    // This is the only place that touches GstBuffer metadata. It converts the
    // hidden GStreamer world back into the runtime JSON callback.
    static GstPadProbeReturn perceptionProbe(GstPad *, GstPadProbeInfo *info, gpointer userData) {
        auto *self = static_cast<Pipeline::Impl *>(userData);
        if (!self || !(GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER)) {
            return GST_PAD_PROBE_OK;
        }

        GstBuffer *buffer = gst_pad_probe_info_get_buffer(info);
        if (!buffer) {
            return GST_PAD_PROBE_OK;
        }

        auto frameResults = opk::FrameResultsMeta::read(buffer);
        if (!frameResults) {
            return GST_PAD_PROBE_OK;
        }

        self->emitPerception(*frameResults);
        return GST_PAD_PROBE_OK;
    }

    // Copy std::function under the mutex, then call it unlocked. This avoids
    // holding our lock while user code runs.
    void emitPerception(const perception::FrameResults &frameResults) {
        Pipeline::FrameResultsCallback callback;
        Pipeline::FrameResultsPacketCallback packetCallback;
        {
            std::lock_guard lock(callbackMutex);
            callback = frameResultsCallback;
            packetCallback = frameResultsPacketCallback;
        }

        if (!callback && !packetCallback) {
            return;
        }

        try {
            const auto packet = perception::serialize(frameResults);
            if (packetCallback) {
                packetCallback(packet);
            }
            if (callback) {
                callback(serializeFrameResultsJson(packet));
            }
        } catch (const std::exception &e) {
            emitError(
                makeError(ErrorFlag::RuntimeError,
                          fmt::format("Failed to serialize FrameResults metadata: {}", e.what())));
        }
    }

    void emitError(const Error &error) {
        Pipeline::ErrorCallback callback;
        {
            std::lock_guard lock(callbackMutex);
            callback = errorCallback;
        }

        if (callback) {
            callback(error);
        }
    }

    void emitEos() {
        Pipeline::EosCallback callback;
        {
            std::lock_guard lock(callbackMutex);
            callback = eosCallback;
        }

        if (callback) {
            callback();
        }
    }
};

Pipeline::Pipeline() : impl(std::make_unique<Impl>()) {}

Pipeline::~Pipeline() = default;

Pipeline::Pipeline(Pipeline &&other) noexcept = default;

Pipeline &Pipeline::operator=(Pipeline &&other) noexcept = default;

Result<Pipeline> Pipeline::fromString(const std::string &description) {
    Pipeline pipeline;
    auto result = pipeline.loadFromString(description);
    if (!result) {
        return tl::unexpected(std::move(result.error()));
    }

    return pipeline;
}

Result<Pipeline> Pipeline::fromJsonFile(const std::string &path) {
    Pipeline pipeline;
    auto result = pipeline.loadFromJsonFile(path);
    if (!result) {
        return tl::unexpected(std::move(result.error()));
    }

    return pipeline;
}

// Build trees often mix real GStreamer plugins and helper libraries in one
// directory, so scanning is best-effort after checking that the path exists.
Result<void> Pipeline::addPluginPath(const std::string &path) {
    ensureGstInitialized();

    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec)) {
        return tl::unexpected(
            makeError(ErrorFlag::FileNotFound,
                      fmt::format("GStreamer plugin path '{}' is not a directory", path)));
    }

    // Build directories can contain helper libraries next to GStreamer plugins.
    // gst_registry_scan_path() may return false when any file cannot be loaded
    // as a plugin, even if useful plugins were discovered. Keep this best-effort
    // and let pipeline parsing/state changes report a hard error if an element
    // is actually unavailable.
    (void)gst_registry_scan_path(gst_registry_get(), path.c_str());
    return {};
}

Result<void> Pipeline::loadFromString(const std::string &description) {
    return impl->loadFromString(description);
}

Result<void> Pipeline::loadFromJsonFile(const std::string &path) {
    return impl->loadFromJsonFile(path);
}

Result<void> Pipeline::start() {
    return start(StartOptions{});
}

Result<void> Pipeline::start(const StartOptions &options) {
    return impl->start(options);
}

Result<void> Pipeline::pause() {
    return impl->setState(GST_STATE_PAUSED, "PAUSED");
}

Result<void> Pipeline::stop() {
    return impl->stop();
}

Result<void> Pipeline::wait() {
    return impl->wait();
}

void Pipeline::onFrameResults(FrameResultsCallback callback) {
    impl->onFrameResults(std::move(callback));
}

void Pipeline::onFrameResultsPacket(FrameResultsPacketCallback callback) {
    impl->onFrameResultsPacket(std::move(callback));
}

void Pipeline::onProgress(ProgressCallback callback) {
    impl->onProgress(std::move(callback));
}

void Pipeline::onError(ErrorCallback callback) {
    impl->onError(std::move(callback));
}

void Pipeline::onEos(EosCallback callback) {
    impl->onEos(std::move(callback));
}

Result<void> Pipeline::attachFrameResultsProbe(const std::string &elementName,
                                               const std::string &padName) {
    return impl->attachFrameResultsProbe(elementName, padName);
}

bool Pipeline::loaded() const noexcept {
    return impl->loaded();
}

} // namespace opk::runtime
