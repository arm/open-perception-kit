/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "PerceptionPacket.h"
#include "TextDisplay.h"
#include "runtime/PerformanceMetrics.h"
#include "runtime/Pipeline.h"

#include <fmt/core.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

// Keep the example-local helpers out of the global namespace. pipeline-exec is
// meant to demonstrate the public OPK runtime API, not define reusable OPK
// utility functions.

struct Options {
    std::string pipelineJsonPath;
    std::string perfCsvPath;
};

struct PluginPath {
    std::string path;
    bool required = false;
};

void printUsage(const char *programName) {
    fmt::print(stderr, "Usage: {} [--perf-csv FILE] <pipeline.json>\n", programName);
}

bool parseOptions(int argc, char **argv, Options &options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--perf-csv") {
            if (i + 1 >= argc) {
                fmt::print(stderr, "--perf-csv requires a file path\n");
                return false;
            }
            options.perfCsvPath = argv[++i];
        } else if (arg.empty() || arg[0] == '-') {
            fmt::print(stderr, "Unknown argument: {}\n", arg);
            return false;
        } else if (options.pipelineJsonPath.empty()) {
            options.pipelineJsonPath = arg;
        } else {
            fmt::print(stderr, "Unexpected extra pipeline argument: {}\n", arg);
            return false;
        }
    }

    if (options.pipelineJsonPath.empty()) {
        fmt::print(stderr, "Missing pipeline JSON path\n");
        return false;
    }

    return true;
}

PluginPath pluginPath() {
    // The OPK GStreamer elements are plugins. When OPK is installed globally,
    // GStreamer may already find them through its normal plugin search path.
    // When running directly from this repository, they live in the Meson output
    // directory, so the example asks opk::runtime::Pipeline to scan that directory.
    //
    // OPK_PLUGIN_PATH gives users a simple override without exposing any
    // GStreamer types in this example source file.
    if (const char *fromEnv = std::getenv("OPK_PLUGIN_PATH")) {
        if (fromEnv[0] != '\0') {
            return {fromEnv, true};
        }
    }

    const char *projectRoot = std::getenv("OPK_PROJECT_ROOT");
    const std::filesystem::path root =
        projectRoot != nullptr && projectRoot[0] != '\0' ? projectRoot : "/work";
    return {
        .path = (root / "development/build-active/meson-out").string(),
        .required = false,
    };
}

template <typename Payload>
void printPayloadBranch(const open_perception_kit::container::envelope &frameResults,
                        std::size_t &printedPayloads) {
    PerceptionPacket::visitFrameResultsPayloads<Payload>(
        frameResults, [&printedPayloads](const Payload &payload) {
            ++printedPayloads;
            fmt::print("{}\n", TextDisplay::formatText(payload));
        });
}

void printTypedPayloadText(const open_perception_kit::container::envelope &frameResults) {
    std::size_t printedPayloads = 0;

    // The packet has already been validated by the example-local PerceptionPacket helper.
    // Each visitor call selects one generated payload root type, and the lambda
    // runs once for every payload of that type in this frame. The display helper
    // formats that one decoded payload into terminal-friendly text.
    printPayloadBranch<open_perception_kit::metadata::FrameContextT>(frameResults, printedPayloads);
    printPayloadBranch<open_perception_kit::metadata::BoxDetectionsT>(frameResults,
                                                                      printedPayloads);
    printPayloadBranch<open_perception_kit::metadata::ObjectTracksT>(frameResults, printedPayloads);
    printPayloadBranch<open_perception_kit::metadata::ClassificationsT>(frameResults,
                                                                        printedPayloads);
    printPayloadBranch<open_perception_kit::metadata::PoseEstimationsT>(frameResults,
                                                                        printedPayloads);
    printPayloadBranch<open_perception_kit::metadata::SegmentationMasksT>(frameResults,
                                                                          printedPayloads);
    printPayloadBranch<open_perception_kit::metadata::ObjectEmbeddingsT>(frameResults,
                                                                         printedPayloads);
    printPayloadBranch<open_perception_kit::metadata::TrackTracesT>(frameResults, printedPayloads);
    printPayloadBranch<open_perception_kit::metadata::PerformanceOverlayT>(frameResults,
                                                                           printedPayloads);

    const auto totalPayloads = frameResults.size();
    if (printedPayloads == 0 && totalPayloads == 0) {
        fmt::print("FrameResults: no payloads\n");
    } else if (printedPayloads < totalPayloads) {
        for (std::size_t i = printedPayloads; i < totalPayloads; ++i) {
            fmt::print("Unknown payload type\n");
        }
    }
}

} // namespace

int main(int argc, char **argv) {
    // pipeline-exec intentionally has the smallest useful interface for the
    // GStreamer-backed runtime layer: one OPK pipeline JSON file, plus optional
    // performance trace CSV output. The JSON format is the same one used by the
    // JSON files under config/pipelines.
    Options options;
    if (!parseOptions(argc, argv, options)) {
        printUsage(argv[0]);
        return 2;
    }

    const std::string &pipelineJsonPath = options.pipelineJsonPath;

    // Make build-tree OPK plugins visible to GStreamer before parsing the
    // pipeline. The runtime wrapper still hides GStreamer types; this call only takes
    // a normal filesystem path.
    const auto plugins = pluginPath();
    std::error_code ec;
    if (plugins.required || std::filesystem::is_directory(plugins.path, ec)) {
        auto pluginPathResult = opk::runtime::Pipeline::addPluginPath(plugins.path);
        if (!pluginPathResult) {
            fmt::print(stderr, "{}\n", pluginPathResult.error().toString());
            return 1;
        }
    }

    // fromJsonFile() reads the OPK pipeline JSON, extracts its "pipeline" field,
    // expands supported environment placeholders like ${NUM_FRAMES:-30}, and
    // builds the underlying GStreamer pipeline from the resulting launch string.
    auto pipelineResult = opk::runtime::Pipeline::fromJsonFile(pipelineJsonPath);
    if (!pipelineResult) {
        fmt::print(stderr, "{}\n", pipelineResult.error().toString());
        return 1;
    }

    // Pipeline is move-only because it owns the hidden GStreamer pipeline and
    // callback probes. Moving it out of Result transfers that ownership into the
    // local variable used below.
    auto pipeline = std::move(*pipelineResult);
    std::atomic_size_t frameResultsCount{0};

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

    // onFrameResultsPacket() is the main reason this example exists. The runtime
    // wrapper installs internal probes that read FrameResults metadata from
    // GStreamer buffers and call this C++ callback with serialized Perception
    // packet bytes. No GstBuffer/GstMeta type is visible to the application.
    pipeline.onFrameResultsPacket(
        [&frameResultsCount, &markCompleted](const std::vector<std::uint8_t> &packet) {
            const size_t currentFrameResults = ++frameResultsCount;

            // Decode exactly at the application boundary where typed semantics are
            // needed. The helper validates the FlatBuffers envelope and checks
            // producer identity before generated SDK payload types are visited.
            auto frameResults = PerceptionPacket::decodeFrameResultsPacket(packet);
            if (!frameResults) {
                fmt::print(stderr, "{}\n", frameResults.error().toString());
                markCompleted(true);
                return;
            }

            // Payload count helps distinguish "nothing was attached" from "payloads
            // exist, but this example does not currently visit their generated type".
            fmt::print("FrameResults {}: packet bytes={}, payloads={}\n",
                       currentFrameResults,
                       packet.size(),
                       frameResults->size());
            printTypedPayloadText(*frameResults);
        });

    // Errors observed by Pipeline's internal bus watcher are reported through
    // the public opk::runtime::Error type. The callback may run from Pipeline's
    // background thread, so the example only prints and signals completion.
    pipeline.onError([&markCompleted](const opk::runtime::Error &error) {
        fmt::print(stderr, "{}\n", error.toString());
        markCompleted(true);
    });

    // EOS is also delivered from Pipeline's internal bus watcher. The callback
    // only signals the example-owned condition variable; the main thread keeps
    // control over when to call stop().
    pipeline.onEos([&markCompleted]() { markCompleted(false); });

    // start() applies StartOptions defaults (Error logging to stderr), moves the
    // hidden GStreamer pipeline to PLAYING, and returns immediately. Buffers now
    // begin to flow and callbacks can fire while the application keeps ownership
    // of this thread.
    if (!options.perfCsvPath.empty()) {
        opk::runtime::PerformanceMetrics::setHistoryEnabled(true);
    }

    auto startResult = pipeline.start();
    if (!startResult) {
        if (!options.perfCsvPath.empty()) {
            opk::runtime::PerformanceMetrics::setHistoryEnabled(false);
        }
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
        if (!options.perfCsvPath.empty()) {
            opk::runtime::PerformanceMetrics::setHistoryEnabled(false);
        }
        fmt::print(stderr, "{}\n", stopResult.error().toString());
        return 1;
    }

    if (!options.perfCsvPath.empty()) {
        const auto performanceSnapshot = opk::runtime::PerformanceMetrics::snapshot();
        opk::runtime::PerformanceMetrics::setHistoryEnabled(false);
        if (!opk::runtime::PerformanceMetrics::writeCsv(options.perfCsvPath)) {
            fmt::print(stderr,
                       "pipeline-exec: failed to write performance CSV: {}\n",
                       options.perfCsvPath);
            return 1;
        }
        const auto metricsWereDropped = performanceSnapshot.droppedMetrics > 0;
        const auto spansWereDropped = performanceSnapshot.droppedSpans > 0;
        const auto historyEventsWereDropped = performanceSnapshot.droppedHistoryEvents > 0;
        const auto threadScopesWereClosedIncorrectly =
            performanceSnapshot.wrongThreadScopeCloses > 0;
        if (metricsWereDropped || spansWereDropped || historyEventsWereDropped ||
            threadScopesWereClosedIncorrectly || performanceSnapshot.threadSlotOverflow) {
            fmt::print(stderr,
                       "pipeline-exec: performance metrics warning: droppedMetrics={} "
                       "droppedSpans={} droppedHistoryEvents={} wrongThreadScopeCloses={} "
                       "threadSlotOverflow={}\n",
                       performanceSnapshot.droppedMetrics,
                       performanceSnapshot.droppedSpans,
                       performanceSnapshot.droppedHistoryEvents,
                       performanceSnapshot.wrongThreadScopeCloses,
                       performanceSnapshot.threadSlotOverflow ? "true" : "false");
        }
        fmt::print(stderr, "pipeline-exec: wrote performance CSV: {}\n", options.perfCsvPath);
    }

    fmt::print(stderr,
               "pipeline-exec: completed, received {} FrameResults callback(s)\n",
               frameResultsCount.load());
    return failedResult ? 1 : 0;
}
