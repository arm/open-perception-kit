/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include "runtime/Logging.h"
#include "runtime/Result.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace opk::runtime {

/**
 * @brief High-level C++ facade for running OPK GStreamer pipelines.
 *
 * Pipeline intentionally hides all GStreamer and internal OPK runtime types
 * from the public runtime API. Client code can build and control a GStreamer-backed
 * OPK pipeline while depending only on runtime headers and normal C++ types.
 *
 * Pipeline owns the wrapped GStreamer pipeline and its installed probes. The
 * wrapper is move-only; callbacks registered on the pipeline are owned by the
 * Pipeline object until replaced, cleared, or the Pipeline is destroyed.
 */
class Pipeline {
  public:
    /**
     * @brief Options controlling a playback run started by start().
     */
    struct StartOptions {
        /**
         * @brief Restart seekable media from the beginning after each completed segment.
         *
         * Looping happens inside the same GStreamer pipeline. start() or wait()
         * reports a NotSupported error when no finite terminal media branch can
         * seek in the time domain.
         */
        bool loop = false;

        /**
         * @brief Process-wide OPK log verbosity applied when playback starts.
         */
        LogLevel logLevel = LogLevel::Error;

        /** @brief Write OPK log records to standard output. */
        bool logToStdout = false;

        /** @brief Write OPK log records to standard error. */
        bool logToStderr = true;

        /** @brief Append OPK log records to the configured log file. */
        bool logToFile = false;
    };

    /**
     * @brief Coarse playback progress observed from the underlying pipeline.
     *
     * Live pipelines and mixed media graphs may not expose a media position or
     * duration. In that case the callback still acts as an execution heartbeat,
     * with hasPosition/hasDuration set to false.
     */
    struct Progress {
        bool hasPosition = false;
        bool hasDuration = false;
        std::int64_t positionNs = 0;
        std::int64_t durationNs = 0;
    };

    /**
     * @brief Called when a buffer carrying FrameResults metadata passes a runtime probe.
     *
     * The callback receives the serialized FrameResults JSON wrapper for that buffer.
     * The string reference is valid only for the duration of the callback; copy it
     * inside the callback if it must be retained. FrameResults callbacks can run
     * from streaming/runtime threads, so callback code should not assume it is on
     * the thread that called start().
     */
    using FrameResultsCallback = std::function<void(const std::string &)>;

    /**
     * @brief Called when a buffer carrying FrameResults metadata passes a runtime probe.
     *
     * The callback receives the serialized Perception FrameResults packet bytes for that
     * buffer. The byte-vector reference is valid only for the duration of the callback;
     * copy it inside the callback if it must be retained. FrameResults callbacks
     * can run from streaming/runtime threads, so callback code should not assume it
     * is on the thread that called start().
     */
    using FrameResultsPacketCallback = std::function<void(const std::vector<std::uint8_t> &)>;

    /**
     * @brief Called periodically while the pipeline is running.
     *
     * Progress callbacks run from Pipeline's background bus watcher thread.
     */
    using ProgressCallback = std::function<void(const Progress &)>;

    /**
     * @brief Called when pipeline execution reports an error.
     *
     * Error callbacks can run from Pipeline's background bus watcher thread.
     */
    using ErrorCallback = std::function<void(const Error &)>;

    /**
     * @brief Called when the pipeline posts EOS.
     *
     * EOS callbacks can run from Pipeline's background bus watcher thread.
     */
    using EosCallback = std::function<void()>;

    /**
     * @brief Constructs an empty pipeline wrapper.
     */
    Pipeline();

    /**
     * @brief Stops and releases the wrapped pipeline.
     */
    ~Pipeline();

    Pipeline(const Pipeline &) = delete;
    Pipeline &operator=(const Pipeline &) = delete;

    Pipeline(Pipeline &&other) noexcept;
    Pipeline &operator=(Pipeline &&other) noexcept;

    /**
     * @brief Creates a pipeline from a GStreamer pipeline-description string.
     * @param description GStreamer launch syntax, e.g. "src ! filter ! sink".
     * @return Loaded pipeline on success, parse/setup error on failure.
     */
    static Result<Pipeline> fromString(const std::string &description);

    /**
     * @brief Creates a pipeline from an OPK pipeline JSON file.
     *
     * The file format matches the versioned JSON files under `config/pipelines`:
     * a MAJOR.MINOR.PATCH `version` string and string `description` are required.
     * The root object must contain a `pipeline` field that is either one nonblank
     * string or an array of string fragments. Array fragments are joined with spaces
     * before parsing as a GStreamer pipeline-description string. The shared preset
     * loader also validates optional metadata; pipeline execution does not implicitly
     * enable looping.
     *
     * @param path Path to an OPK pipeline JSON file.
     * @return Loaded pipeline on success, file/JSON/parse/setup error on failure.
     */
    static Result<Pipeline> fromJsonFile(const std::string &path);

    /**
     * @brief Adds a directory to GStreamer's plugin registry scan path.
     *
     * This is useful for applications that run directly from an OPK build tree,
     * where OPK elements live in `development/build/meson-out` rather than an
     * installed GStreamer plugin directory.
     *
     * @param path Directory containing GStreamer plugin shared objects.
     * @return Success or an error if the path is invalid.
     */
    static Result<void> addPluginPath(const std::string &path);

    /**
     * @brief Replaces the wrapped pipeline with one parsed from a string.
     *
     * After loading, the implementation automatically installs perception probes
     * on terminal sink pads where possible. Use attachFrameResultsProbe() for a
     * specific named element/pad when a pipeline needs tighter control.
     *
     * @param description GStreamer launch syntax, e.g. "src ! filter ! sink".
     * @return Success or parse/setup error.
     */
    Result<void> loadFromString(const std::string &description);

    /**
     * @brief Replaces the wrapped pipeline with one loaded from an OPK pipeline JSON file.
     * @param path Path to an OPK pipeline JSON file.
     * @return Success or file/JSON/parse/setup error.
     */
    Result<void> loadFromJsonFile(const std::string &path);

    /**
     * @brief Starts pipeline playback and returns immediately.
     *
     * Pipeline owns a small background bus watcher thread after start(). EOS
     * and ERROR are reported through onEos(), onError(), and wait().
     * FrameResults callbacks may also begin running before start() returns if the
     * pipeline produces buffers immediately.
     */
    Result<void> start();

    /**
     * @brief Starts pipeline playback with explicit playback options.
     * @param options Playback and process-wide logging behavior for this run.
     * @return Success after playback starts, or a setup/state-change error.
     *
     * When looping is enabled, EOS is replaced by time-segment seeks and is not
     * reported through onEos() or wait() between iterations. The run remains
     * active until stop() is called or an error occurs.
     *
     * Logging configuration is process-wide. Starting another Pipeline with
     * different logging options replaces the active process settings.
     */
    Result<void> start(const StartOptions &options);

    /**
     * @brief Pauses pipeline playback by moving it to PAUSED.
     */
    Result<void> pause();

    /**
     * @brief Stops the pipeline and releases streaming resources by moving it to NULL.
     *
     * stop() also requests the background bus watcher to exit and joins it when
     * called from another thread.
     */
    Result<void> stop();

    /**
     * @brief Blocks until the background bus watcher observes EOS, ERROR, or stop().
     *
     * This is a convenience helper for simple command-line tools. Applications
     * with their own event loop can skip wait() and use onEos()/onError() instead.
     */
    Result<void> wait();

    /**
     * @brief Registers a callback for FrameResults metadata seen by runtime probes.
     *
     * The callback object is moved into the Pipeline. Objects captured by the
     * callback remain owned by the caller and must outlive every callback
     * invocation.
     */
    void onFrameResults(FrameResultsCallback callback);

    /**
     * @brief Registers a packet callback for FrameResults metadata seen by runtime probes.
     *
     * The callback object is moved into the Pipeline. Objects captured by the
     * callback remain owned by the caller and must outlive every callback
     * invocation.
     */
    void onFrameResultsPacket(FrameResultsPacketCallback callback);

    /**
     * @brief Registers a callback for coarse execution progress.
     */
    void onProgress(ProgressCallback callback);

    /**
     * @brief Registers a callback for pipeline errors observed by the bus watcher.
     */
    void onError(ErrorCallback callback);

    /**
     * @brief Registers a callback for EOS observed by the bus watcher.
     */
    void onEos(EosCallback callback);

    /**
     * @brief Adds a FrameResults metadata probe to a named element pad.
     *
     * The pipeline-description string can name elements using standard
     * GStreamer syntax, for example `opkinfer name=detector ...`. This method
     * can then attach to `detector`'s `src` pad without exposing GStreamer types.
     *
     * @param elementName Name of an element inside the parsed pipeline.
     * @param padName Static pad name to probe, usually "src" or "sink".
     * @return Success or an error if the pipeline, element, or pad is missing.
     */
    Result<void> attachFrameResultsProbe(const std::string &elementName,
                                         const std::string &padName = "src");

    /**
     * @brief Returns true when a pipeline has been loaded successfully.
     */
    bool loaded() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace opk::runtime
