/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "api/Pipeline.h"

#include <fmt/core.h>
#include <fmt/ranges.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

// Keep the example-local helpers out of the global namespace. pipeline-run is
// meant to demonstrate the application-facing API, not define reusable PEK
// utility functions.

void printUsage(const char *programName) {
    fmt::print(stderr, "Usage: {} <pipeline.json>\n", programName);
}

std::string pluginPath() {
    // The PEK GStreamer elements are plugins. When PEK is installed globally,
    // GStreamer may already find them through its normal plugin search path.
    // When running directly from this repository, they live in the Meson output
    // directory, so the example asks pek::api::Pipeline to scan that directory.
    //
    // PEK_PLUGIN_PATH gives users a simple override without exposing any
    // GStreamer API in this example source file.
    if (const char *fromEnv = std::getenv("PEK_PLUGIN_PATH")) {
        if (fromEnv[0] != '\0') {
            return fromEnv;
        }
    }

    return "/work/development/build/meson-out";
}

} // namespace

int main(int argc, char **argv) {
    // pipeline-run intentionally has the smallest useful interface for the
    // GStreamer-backed API layer: one PEK pipeline JSON file. The JSON format is
    // the same one used by the JSON files under config/pipelines.
    if (argc != 2) {
        printUsage(argv[0]);
        return 2;
    }

    const std::string pipelineJsonPath = argv[1];

    // Make build-tree PEK plugins visible to GStreamer before parsing the
    // pipeline. The public API still hides GStreamer types; this call only takes
    // a normal filesystem path.
    auto pluginPathResult = pek::api::Pipeline::addPluginPath(pluginPath());
    if (!pluginPathResult) {
        fmt::print(stderr, "{}\n", pluginPathResult.error().toString());
        return 1;
    }

    // fromJsonFile() reads the PEK pipeline JSON, extracts its "pipeline" field,
    // expands supported environment placeholders like ${NUM_FRAMES:-30}, and
    // builds the underlying GStreamer pipeline from the resulting launch string.
    auto pipelineResult = pek::api::Pipeline::fromJsonFile(pipelineJsonPath);
    if (!pipelineResult) {
        fmt::print(stderr, "{}\n", pipelineResult.error().toString());
        return 1;
    }

    // Pipeline is move-only because it owns the hidden GStreamer pipeline and
    // callback probes. Moving it out of Result transfers that ownership into the
    // local variable used below.
    auto pipeline = std::move(*pipelineResult);
    size_t perceptionCount = 0;

    // onPerception() is the main reason this example exists. The API installs
    // internal probes that read Perception metadata from GStreamer buffers and
    // call this C++ callback with serialized JSON. No internal PEK Perception
    // type and no GstBuffer/GstMeta type is visible to the application.
    pipeline.onPerception([&perceptionCount](const std::string &perceptionJson) {
        ++perceptionCount;

        // A pipeline can contain several inference stages. Each stage typically
        // adds one Perception layer. The public API gives us JSON, so the example
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
            fmt::print(stderr, "pipeline-run: failed to parse perception JSON: {}\n", e.what());
        }

        fmt::print("Perception {}: layers=[{}]\n", perceptionCount, fmt::join(layerNames, ", "));
    });

    // Errors observed by the pipeline bus are reported through the API-local
    // Error type, so applications do not include internal PEK runtime headers.
    pipeline.onError(
        [](const pek::api::Error &error) { fmt::print(stderr, "{}\n", error.toString()); });

    // start() moves the hidden GStreamer pipeline to PLAYING. For finite inputs,
    // buffers now begin to flow and callbacks can fire. For live inputs, this
    // would keep running until stopped from another thread or an error occurs.
    auto startResult = pipeline.start();
    if (!startResult) {
        fmt::print(stderr, "{}\n", startResult.error().toString());
        return 1;
    }

    // wait() blocks on the hidden pipeline bus until EOS or ERROR. Image/video
    // test pipelines with a bounded source eventually post EOS; camera pipelines
    // generally do not unless stopped externally.
    auto waitResult = pipeline.wait();
    if (!waitResult) {
        (void)pipeline.stop();
        return 1;
    }

    // stop() moves the pipeline back to NULL so GStreamer releases streaming
    // resources before the wrapper object is destroyed.
    auto stopResult = pipeline.stop();
    if (!stopResult) {
        fmt::print(stderr, "{}\n", stopResult.error().toString());
        return 1;
    }

    fmt::print(
        stderr, "pipeline-run: completed, received {} perception result(s)\n", perceptionCount);
    return 0;
}
