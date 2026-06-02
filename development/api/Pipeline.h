/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "api/Result.h"

#include <functional>
#include <memory>
#include <string>

namespace pek::api {

/**
 * @brief High-level C++ facade for running PEK GStreamer pipelines.
 *
 * Pipeline intentionally hides all GStreamer and internal PEK runtime types
 * from the public API. Client code can build and control a GStreamer-backed
 * PEK pipeline while depending only on API headers and normal C++ types.
 */
class Pipeline {
  public:
    /**
     * @brief Called when a buffer carrying Perception metadata passes an API probe.
     *
     * The callback receives the serialized Perception JSON for that buffer. The
     * string reference is valid only for the duration of the callback; copy it
     * inside the callback if it must be retained.
     */
    using PerceptionCallback = std::function<void(const std::string &)>;

    /**
     * @brief Called when pipeline execution reports an error.
     */
    using ErrorCallback = std::function<void(const Error &)>;

    /**
     * @brief Called when the pipeline posts EOS.
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
     * @brief Creates a pipeline from a PEK pipeline JSON file.
     *
     * The file format matches the JSON files under `config/pipelines`: the root object must
     * contain a `pipeline` field that is either one string or an array of string
     * fragments. Array fragments are joined with spaces before parsing as a
     * GStreamer pipeline-description string.
     *
     * @param path Path to a PEK pipeline JSON file.
     * @return Loaded pipeline on success, file/JSON/parse/setup error on failure.
     */
    static Result<Pipeline> fromJsonFile(const std::string &path);

    /**
     * @brief Adds a directory to GStreamer's plugin registry scan path.
     *
     * This is useful for applications that run directly from a PEK build tree,
     * where PEK elements live in `development/build/meson-out` rather than an
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
     * on terminal sink pads where possible. Use attachPerceptionProbe() for a
     * specific named element/pad when a pipeline needs tighter control.
     *
     * @param description GStreamer launch syntax, e.g. "src ! filter ! sink".
     * @return Success or parse/setup error.
     */
    Result<void> loadFromString(const std::string &description);

    /**
     * @brief Replaces the wrapped pipeline with one loaded from a PEK pipeline JSON file.
     * @param path Path to a PEK pipeline JSON file.
     * @return Success or file/JSON/parse/setup error.
     */
    Result<void> loadFromJsonFile(const std::string &path);

    /**
     * @brief Starts pipeline playback and returns immediately.
     *
     * Pipeline owns a small background bus watcher thread after start(). EOS
     * and ERROR are reported through onEos(), onError(), and wait().
     */
    Result<void> start();

    /**
     * @brief Pauses pipeline playback by moving it to PAUSED.
     */
    Result<void> pause();

    /**
     * @brief Stops the pipeline and releases streaming resources by moving it to NULL.
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
     * @brief Registers a callback for Perception metadata seen by API probes.
     */
    void onPerception(PerceptionCallback callback);

    /**
     * @brief Registers a callback for pipeline errors observed by the bus watcher.
     */
    void onError(ErrorCallback callback);

    /**
     * @brief Registers a callback for EOS observed by the bus watcher.
     */
    void onEos(EosCallback callback);

    /**
     * @brief Adds a Perception metadata probe to a named element pad.
     *
     * The pipeline-description string can name elements using standard
     * GStreamer syntax, for example `pekinfer name=detector ...`. This method
     * can then attach to `detector`'s `src` pad without exposing GStreamer types.
     *
     * @param elementName Name of an element inside the parsed pipeline.
     * @param padName Static pad name to probe, usually "src" or "sink".
     * @return Success or an error if the pipeline, element, or pad is missing.
     */
    Result<void> attachPerceptionProbe(const std::string &elementName,
                                       const std::string &padName = "src");

    /**
     * @brief Returns true when a pipeline has been loaded successfully.
     */
    bool loaded() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace pek::api
