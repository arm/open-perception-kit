/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "api/Pipeline.h"

#include "gst/PerceptionMeta.h"
#include "pek/PerceptionSerializer.h"

#include <fmt/core.h>

#include <gst/gst.h>

#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace pek::api {
namespace {

// GStreamer initialization is process-global. Hide it here so API users do not
// need to call gst_init() themselves.
void ensureGstInitialized() {
    static std::once_flag once;
    std::call_once(once, []() { gst_init(nullptr, nullptr); });
}

Error makeApiError(ErrorFlag flag, const std::string &message) {
    return Error(flag, message);
}

std::string gstErrorMessage(GError *error, const gchar *debugInfo) {
    std::string message = error && error->message ? error->message : "unknown GStreamer error";
    if (debugInfo && debugInfo[0] != '\0') {
        message += fmt::format(" ({})", debugInfo);
    }
    return message;
}

bool isVarStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool isVarChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// Pipeline JSON files use the same small environment-placeholder syntax as
// pek-menu, for example ${NUM_FRAMES:-30}. Expand it before gst_parse_launch().
Result<std::string> expandPipelineDescription(const std::string &description) {
    std::string expanded;
    expanded.reserve(description.size());

    for (size_t i = 0; i < description.size();) {
        if (description[i] != '$' || i + 1 >= description.size() || description[i + 1] != '{') {
            expanded.push_back(description[i]);
            ++i;
            continue;
        }

        size_t nameStart = i + 2;
        if (nameStart >= description.size() || !isVarStart(description[nameStart])) {
            expanded.push_back(description[i]);
            ++i;
            continue;
        }

        size_t nameEnd = nameStart + 1;
        while (nameEnd < description.size() && isVarChar(description[nameEnd])) {
            ++nameEnd;
        }

        const std::string variableName = description.substr(nameStart, nameEnd - nameStart);
        const char *rawValue = std::getenv(variableName.c_str());
        const std::string value = rawValue ? std::string(rawValue) : std::string();

        if (nameEnd < description.size() && description[nameEnd] == '}') {
            expanded += value;
            i = nameEnd + 1;
            continue;
        }

        if (nameEnd + 2 < description.size() && description[nameEnd] == ':' &&
            description[nameEnd + 1] == '-') {
            const size_t defaultStart = nameEnd + 2;
            const size_t end = description.find('}', defaultStart);
            if (end == std::string::npos) {
                return tl::make_unexpected(makeApiError(
                    ErrorFlag::ParseError,
                    fmt::format("Unterminated pipeline variable default for '{}'", variableName)));
            }

            expanded +=
                value.empty() ? description.substr(defaultStart, end - defaultStart) : value;
            i = end + 1;
            continue;
        }

        if (nameEnd + 1 < description.size() && description[nameEnd] == '?') {
            const size_t messageStart = nameEnd + 1;
            const size_t end = description.find('}', messageStart);
            if (end == std::string::npos) {
                return tl::make_unexpected(makeApiError(
                    ErrorFlag::ParseError,
                    fmt::format("Unterminated required pipeline variable '{}'", variableName)));
            }

            if (value.empty()) {
                std::string message = description.substr(messageStart, end - messageStart);
                if (message.empty()) {
                    message = fmt::format("Environment variable '{}' is required", variableName);
                }
                return tl::make_unexpected(makeApiError(ErrorFlag::InvalidArgument, message));
            }

            expanded += value;
            i = end + 1;
            continue;
        }

        expanded.push_back(description[i]);
        ++i;
    }

    return expanded;
}

// Read PEK pipeline JSON and turn its "pipeline" field into one launch string.
// The field may be a single string or an array of string fragments.
Result<std::string> loadPipelineDescriptionFromJsonFile(const std::string &path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return tl::make_unexpected(
            makeApiError(ErrorFlag::FileNotFound,
                         fmt::format("Failed to open PEK pipeline JSON file '{}'", path)));
    }

    try {
        nlohmann::json document;
        input >> document;

        if (!document.is_object()) {
            return tl::make_unexpected(makeApiError(
                ErrorFlag::InvalidPipeline,
                fmt::format("PEK pipeline JSON '{}' must contain a root object", path)));
        }

        if (!document.contains("pipeline")) {
            return tl::make_unexpected(makeApiError(
                ErrorFlag::InvalidPipeline,
                fmt::format("PEK pipeline JSON '{}' is missing required 'pipeline' field", path)));
        }

        const auto &pipeline = document["pipeline"];
        if (pipeline.is_string()) {
            auto description = pipeline.get<std::string>();
            if (description.empty()) {
                return tl::make_unexpected(makeApiError(
                    ErrorFlag::InvalidPipeline,
                    fmt::format("PEK pipeline JSON '{}' contains an empty 'pipeline' string",
                                path)));
            }
            return description;
        }

        if (!pipeline.is_array()) {
            return tl::make_unexpected(makeApiError(
                ErrorFlag::InvalidPipeline,
                fmt::format(
                    "PEK pipeline JSON '{}' field 'pipeline' must be a string or array of strings",
                    path)));
        }

        std::string joined;
        bool first = true;
        for (const auto &partValue : pipeline) {
            if (!partValue.is_string()) {
                return tl::make_unexpected(makeApiError(
                    ErrorFlag::InvalidPipeline,
                    fmt::format(
                        "PEK pipeline JSON '{}' field 'pipeline' array must contain only strings",
                        path)));
            }

            auto part = partValue.get<std::string>();
            if (part.empty()) {
                continue;
            }

            if (!first) {
                joined.push_back(' ');
            }
            joined += part;
            first = false;
        }

        if (joined.empty()) {
            return tl::make_unexpected(makeApiError(
                ErrorFlag::InvalidPipeline,
                fmt::format("PEK pipeline JSON '{}' contains an empty 'pipeline' array", path)));
        }

        return joined;
    } catch (const nlohmann::json::exception &e) {
        return tl::make_unexpected(makeApiError(
            ErrorFlag::ParseError,
            fmt::format("Failed to parse PEK pipeline JSON '{}': {}", path, e.what())));
    }
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

    ~Impl() {
        clearPipeline();
    }

    // Parse a GStreamer launch string and install default perception probes.
    Result<void> loadFromString(const std::string &description) {
        ensureGstInitialized();
        clearPipeline();

        auto expandedDescription = expandPipelineDescription(description);
        if (!expandedDescription) {
            return tl::make_unexpected(std::move(expandedDescription.error()));
        }

        GError *parseError = nullptr;
        GstElement *parsed = gst_parse_launch(expandedDescription->c_str(), &parseError);
        if (!parsed) {
            std::string message = parseError && parseError->message ? parseError->message
                                                                    : "failed to parse pipeline";
            if (parseError) {
                g_error_free(parseError);
            }
            return tl::make_unexpected(makeApiError(ErrorFlag::InvalidPipeline, message));
        }

        if (parseError) {
            std::string message = parseError->message ? parseError->message
                                                      : "pipeline parsed with recoverable errors";
            g_error_free(parseError);
            gst_object_unref(parsed);
            return tl::make_unexpected(makeApiError(ErrorFlag::InvalidPipeline, message));
        }

        pipeline = parsed;
        installDefaultPerceptionProbes();
        return {};
    }

    Result<void> loadFromJsonFile(const std::string &path) {
        auto description = loadPipelineDescriptionFromJsonFile(path);
        if (!description) {
            return tl::make_unexpected(std::move(description.error()));
        }

        return loadFromString(*description);
    }

    // start(), pause(), and stop() all use this small shared state helper.
    Result<void> setState(GstState state, const char *stateName) {
        if (!pipeline) {
            return tl::make_unexpected(
                makeApiError(ErrorFlag::InvalidArgument, "No pipeline has been loaded"));
        }

        const GstStateChangeReturn ret = gst_element_set_state(pipeline, state);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            return tl::make_unexpected(
                makeApiError(ErrorFlag::RuntimeError,
                             fmt::format("Failed to set pipeline state to {}", stateName)));
        }

        return {};
    }

    // Block on the pipeline bus until EOS or ERROR. This is enough for simple
    // finite examples; live apps can call start()/stop() from their own control flow.
    Result<void> wait() {
        if (!pipeline) {
            return tl::make_unexpected(
                makeApiError(ErrorFlag::InvalidArgument, "No pipeline has been loaded"));
        }

        GstBus *bus = gst_element_get_bus(pipeline);
        if (!bus) {
            return tl::make_unexpected(makeApiError(
                ErrorFlag::RuntimeError, "Loaded element does not expose a GStreamer bus"));
        }

        while (true) {
            GstMessage *message = gst_bus_timed_pop_filtered(
                bus,
                GST_CLOCK_TIME_NONE,
                static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

            if (!message) {
                continue;
            }

            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
                gst_message_unref(message);
                gst_object_unref(bus);
                return {};
            }

            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
                GError *error = nullptr;
                gchar *debugInfo = nullptr;
                gst_message_parse_error(message, &error, &debugInfo);

                auto apiError =
                    makeApiError(ErrorFlag::RuntimeError, gstErrorMessage(error, debugInfo));
                emitError(apiError);

                if (error) {
                    g_error_free(error);
                }
                if (debugInfo) {
                    g_free(debugInfo);
                }
                gst_message_unref(message);
                gst_object_unref(bus);
                return tl::make_unexpected(std::move(apiError));
            }

            gst_message_unref(message);
        }
    }

    void onPerception(Pipeline::PerceptionCallback callback) {
        std::lock_guard lock(callbackMutex);
        perceptionCallback = std::move(callback);
    }

    void onError(Pipeline::ErrorCallback callback) {
        std::lock_guard lock(callbackMutex);
        errorCallback = std::move(callback);
    }

    // Manual hook for users who want perception at a specific named element
    // instead of the automatically detected terminal sinks.
    Result<void> attachPerceptionProbe(const std::string &elementName, const std::string &padName) {
        if (!pipeline) {
            return tl::make_unexpected(
                makeApiError(ErrorFlag::InvalidArgument, "No pipeline has been loaded"));
        }
        if (!GST_IS_BIN(pipeline)) {
            return tl::make_unexpected(
                makeApiError(ErrorFlag::InvalidPipeline, "Loaded pipeline is not a GStreamer bin"));
        }

        GstElement *element = gst_bin_get_by_name(GST_BIN(pipeline), elementName.c_str());
        if (!element) {
            return tl::make_unexpected(makeApiError(
                ErrorFlag::InvalidArgument,
                fmt::format("Element '{}' was not found in the pipeline", elementName)));
        }

        GstPad *pad = gst_element_get_static_pad(element, padName.c_str());
        gst_object_unref(element);
        if (!pad) {
            return tl::make_unexpected(makeApiError(
                ErrorFlag::InvalidArgument,
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
    Pipeline::PerceptionCallback perceptionCallback;
    Pipeline::ErrorCallback errorCallback;

    // Release the current pipeline and any probes before loading a new one.
    void clearPipeline() {
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
    // hidden GStreamer world back into the public JSON callback.
    static GstPadProbeReturn perceptionProbe(GstPad *, GstPadProbeInfo *info, gpointer userData) {
        auto *self = static_cast<Pipeline::Impl *>(userData);
        if (!self || !(GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER)) {
            return GST_PAD_PROBE_OK;
        }

        GstBuffer *buffer = gst_pad_probe_info_get_buffer(info);
        if (!buffer) {
            return GST_PAD_PROBE_OK;
        }

        auto perception = pek::PerceptionMeta::read(buffer);
        if (!perception) {
            return GST_PAD_PROBE_OK;
        }

        self->emitPerception(*perception);
        return GST_PAD_PROBE_OK;
    }

    // Copy std::function under the mutex, then call it unlocked. This avoids
    // holding our lock while user code runs.
    void emitPerception(const pek::Perception &perception) {
        Pipeline::PerceptionCallback callback;
        {
            std::lock_guard lock(callbackMutex);
            callback = perceptionCallback;
        }

        if (!callback) {
            return;
        }

        try {
            const nlohmann::json perceptionJson = perception;
            const std::string serializedPerception = perceptionJson.dump();
            callback(serializedPerception);
        } catch (const std::exception &e) {
            emitError(
                makeApiError(ErrorFlag::RuntimeError,
                             fmt::format("Failed to serialize Perception metadata: {}", e.what())));
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
};

Pipeline::Pipeline() : impl(std::make_unique<Impl>()) {}

Pipeline::~Pipeline() = default;

Pipeline::Pipeline(Pipeline &&other) noexcept = default;

Pipeline &Pipeline::operator=(Pipeline &&other) noexcept = default;

Result<Pipeline> Pipeline::fromString(const std::string &description) {
    Pipeline pipeline;
    auto result = pipeline.loadFromString(description);
    if (!result) {
        return tl::make_unexpected(std::move(result.error()));
    }

    return pipeline;
}

Result<Pipeline> Pipeline::fromJsonFile(const std::string &path) {
    Pipeline pipeline;
    auto result = pipeline.loadFromJsonFile(path);
    if (!result) {
        return tl::make_unexpected(std::move(result.error()));
    }

    return pipeline;
}

// Build trees often mix real GStreamer plugins and helper libraries in one
// directory, so scanning is best-effort after checking that the path exists.
Result<void> Pipeline::addPluginPath(const std::string &path) {
    ensureGstInitialized();

    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec)) {
        return tl::make_unexpected(
            makeApiError(ErrorFlag::FileNotFound,
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
    return impl->setState(GST_STATE_PLAYING, "PLAYING");
}

Result<void> Pipeline::pause() {
    return impl->setState(GST_STATE_PAUSED, "PAUSED");
}

Result<void> Pipeline::stop() {
    return impl->setState(GST_STATE_NULL, "NULL");
}

Result<void> Pipeline::wait() {
    return impl->wait();
}

void Pipeline::onPerception(PerceptionCallback callback) {
    impl->onPerception(std::move(callback));
}

void Pipeline::onError(ErrorCallback callback) {
    impl->onError(std::move(callback));
}

Result<void> Pipeline::attachPerceptionProbe(const std::string &elementName,
                                             const std::string &padName) {
    return impl->attachPerceptionProbe(elementName, padName);
}

bool Pipeline::loaded() const noexcept {
    return impl->loaded();
}

} // namespace pek::api
