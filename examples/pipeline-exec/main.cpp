/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "runtime/Pipeline.h"

#include <fmt/core.h>
#include <fmt/ranges.h>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

// Keep the example-local helpers out of the global namespace. pipeline-exec is
// meant to demonstrate the public PEK runtime API, not define reusable PEK
// utility functions.

void printUsage(const char *programName) {
    fmt::print(stderr, "Usage: {} <pipeline.json>\n", programName);
}

std::string pluginPath() {
    // The PEK GStreamer elements are plugins. When PEK is installed globally,
    // GStreamer may already find them through its normal plugin search path.
    // When running directly from this repository, they live in the Meson output
    // directory, so the example asks pek::runtime::Pipeline to scan that directory.
    //
    // PEK_PLUGIN_PATH gives users a simple override without exposing any
    // GStreamer types in this example source file.
    if (const char *fromEnv = std::getenv("PEK_PLUGIN_PATH")) {
        if (fromEnv[0] != '\0') {
            return fromEnv;
        }
    }

    return "/work/development/build/meson-out";
}

} // namespace

int main(int argc, char **argv) {
    // pipeline-exec intentionally has the smallest useful interface for the
    // GStreamer-backed runtime layer: one PEK pipeline JSON file. The JSON format is
    // the same one used by the JSON files under config/pipelines.
    if (argc != 2) {
        printUsage(argv[0]);
        return 2;
    }

    const std::string pipelineJsonPath = argv[1];

    // Make build-tree PEK plugins visible to GStreamer before parsing the
    // pipeline. The runtime wrapper still hides GStreamer types; this call only takes
    // a normal filesystem path.
    auto pluginPathResult = pek::runtime::Pipeline::addPluginPath(pluginPath());
    if (!pluginPathResult) {
        fmt::print(stderr, "{}\n", pluginPathResult.error().toString());
        return 1;
    }

    // fromJsonFile() reads the PEK pipeline JSON, extracts its "pipeline" field,
    // expands supported environment placeholders like ${NUM_FRAMES:-30}, and
    // builds the underlying GStreamer pipeline from the resulting launch string.
    auto pipelineResult = pek::runtime::Pipeline::fromJsonFile(pipelineJsonPath);
    if (!pipelineResult) {
        fmt::print(stderr, "{}\n", pipelineResult.error().toString());
        return 1;
    }

    // Pipeline is move-only because it owns the hidden GStreamer pipeline and
    // callback probes. Moving it out of Result transfers that ownership into the
    // local variable used below.
    auto pipeline = std::move(*pipelineResult);
    std::atomic_size_t perceptionCount{0};

    // This condition variable is deliberately owned by the example, not by
    // Pipeline. A GUI app could keep its normal UI event loop. The Pipeline
    // only reports EOS/ERROR.
    std::mutex completionMutex;
    std::condition_variable completionCv;
    bool completed = false;
    bool failed = false;

    auto markCompleted = [&](bool failure) {
        {
            std::lock_guard lock(completionMutex);
            failed = failed || failure;
            completed = true;
        }
        completionCv.notify_one();
    };

    // onPerception() is the main reason this example exists. The runtime wrapper installs
    // internal probes that read Perception metadata from GStreamer buffers and
    // call this C++ callback with serialized JSON. No internal PEK Perception
    // type and no GstBuffer/GstMeta type is visible to the application.
    pipeline.onPerception([&perceptionCount](const std::string &perceptionJson) {
        const size_t currentPerception = ++perceptionCount;

        // A pipeline can contain several inference stages. Each stage typically
        // adds one Perception layer. The runtime wrapper gives us JSON, so the example
        // parses only the small part it wants to print: layers[].contentType.
        std::vector<std::string> layerNames;
        try {
            const auto document = nlohmann::json::parse(perceptionJson);
            const auto layers = document.find("layers");
            if (layers != document.end() && layers->is_array()) {
                layerNames.reserve(layers->size());
                for (const auto &layer : *layers) {
                    const auto contentType = layer.find("contentType");
                    if (contentType != layer.end() && contentType->is_string() &&
                        !contentType->get_ref<const std::string &>().empty()) {
                        layerNames.push_back(contentType->get<std::string>());
                    } else {
                        layerNames.push_back("<unknown>");
                    }
                }
            }
        } catch (const nlohmann::json::exception &e) {
            fmt::print(stderr, "pipeline-exec: failed to parse perception JSON: {}\n", e.what());
        }

        fmt::print("Perception {}: layers=[{}]\n", currentPerception, fmt::join(layerNames, ", "));
    });

    // Errors observed by Pipeline's internal bus watcher are reported through
    // the public pek::runtime::Error type. The callback may run from Pipeline's
    // background thread, so the example only prints and signals completion.
    pipeline.onError([&markCompleted](const pek::runtime::Error &error) {
        fmt::print(stderr, "{}\n", error.toString());
        markCompleted(true);
    });

    // EOS is also delivered from Pipeline's internal bus watcher. The callback
    // only signals the example-owned condition variable; the main thread keeps
    // control over when to call stop().
    pipeline.onEos([&markCompleted]() { markCompleted(false); });

    // start() moves the hidden GStreamer pipeline to PLAYING and returns
    // immediately. Buffers now begin to flow and callbacks can fire while the
    // application keeps ownership of this thread.
    auto startResult = pipeline.start();
    if (!startResult) {
        fmt::print(stderr, "{}\n", startResult.error().toString());
        return 1;
    }

    // For this finite example we wait on our own condition variable until the
    // callbacks report EOS or ERROR. A larger app would usually replace this
    // block with its own control loop.
    bool failedResult = false;
    {
        std::unique_lock lock(completionMutex);
        completionCv.wait(lock, [&completed]() { return completed; });
        failedResult = failed;
    }

    // stop() moves the pipeline back to NULL so GStreamer releases streaming
    // resources before the wrapper object is destroyed.
    auto stopResult = pipeline.stop();
    if (!stopResult) {
        fmt::print(stderr, "{}\n", stopResult.error().toString());
        return 1;
    }

    fmt::print(stderr,
               "pipeline-exec: completed, received {} perception result(s)\n",
               perceptionCount.load());
    return failedResult ? 1 : 0;
}
