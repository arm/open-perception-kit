/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "runtime/Pipeline.h"

#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
constexpr const char *SCHEMA_ID = "expkits_yolo_video_benchmark.v3";
constexpr std::size_t WARMUP_FRAMES = 1;
constexpr int IMG_SIZE = 320;

void printUsage(const char *programName) {
    fmt::print(stderr,
               "Usage: {} --opchain <opchain.json> --video <video.mp4> "
               "--source-manifest <video-source.json> --summary <summary.json>\n",
               programName);
}

std::map<std::string, std::string> parseFlagArgs(int argc, char **argv) {
    std::map<std::string, std::string> args;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc)
            return {};
        args[argv[i]] = argv[i + 1];
    }
    return args;
}

std::string quotePipelineValue(const std::string &value) {
    std::string quoted = "\"";
    for (const char c : value) {
        if (c == '\\' || c == '"')
            quoted += '\\';
        quoted += c;
    }
    return quoted + '"';
}

double elapsedMs(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

nlohmann::json loadJson(const std::string &path) {
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("Failed to open JSON file: " + path);
    return nlohmann::json::parse(input);
}

std::string pluginPath() {
    if (const char *fromEnv = std::getenv("PEK_PLUGIN_PATH")) {
        if (fromEnv[0] != '\0')
            return fromEnv;
    }
    return "/work/development/build/meson-out";
}

} // namespace

int main(int argc, char **argv) {
    try {
        const auto args = parseFlagArgs(argc, argv);
        for (const auto *required : {"--opchain", "--video", "--source-manifest", "--summary"}) {
            if (!args.contains(required)) {
                printUsage(argv[0]);
                return 2;
            }
        }

        const auto source = loadJson(args.at("--source-manifest"));
        if (source.value("schema", "") != "expkits_yolo_video_source.v1")
            throw std::runtime_error("Unsupported video source manifest");
        const auto expectedFrames = source.at("frame_count").get<std::size_t>();
        if (expectedFrames <= WARMUP_FRAMES)
            throw std::runtime_error("Video frame count must be greater than the warmup count");

        auto pluginResult = pek::runtime::Pipeline::addPluginPath(pluginPath());
        if (!pluginResult)
            throw std::runtime_error(pluginResult.error().toString());

        // Decode once into a full-video raw queue. The tee then replays those
        // preloaded BGRA buffers through one warmup and one measured branch.
        const auto video = quotePipelineValue(args.at("--video"));
        const std::string warmupCaps = "video/x-raw,format=BGRA,pek-benchmark-pass=(string)warmup";
        const std::string measuredCaps =
            "video/x-raw,format=BGRA,pek-benchmark-pass=(string)measured";
        const auto description =
            fmt::format("concat name=video_sequence ! "
                        "pekinfer opchain-path={} active=true ! "
                        "switchbin num-paths=2 path0::element={} path0::caps={} "
                        "path1::caps={} ! fakesink async=false sync=false "
                        "filesrc location={} ! decodebin ! videoconvert ! "
                        "video/x-raw,format=BGRA ! "
                        "queue max-size-buffers={} max-size-bytes=0 max-size-time=0 "
                        "min-threshold-buffers={} ! tee name=preloaded_video "
                        "preloaded_video. ! queue max-size-buffers={} max-size-bytes=0 "
                        "max-size-time=0 ! capssetter caps={} replace=false ! "
                        "video_sequence.sink_0 "
                        "preloaded_video. ! queue max-size-buffers={} max-size-bytes=0 "
                        "max-size-time=0 ! capssetter caps={} replace=false ! "
                        "video_sequence.sink_1",
                        quotePipelineValue(args.at("--opchain")),
                        quotePipelineValue("valve drop=true drop-mode=forward-sticky-events"),
                        quotePipelineValue(warmupCaps),
                        quotePipelineValue(measuredCaps),
                        video,
                        expectedFrames,
                        expectedFrames,
                        expectedFrames,
                        quotePipelineValue(warmupCaps),
                        expectedFrames,
                        quotePipelineValue(measuredCaps));

        const auto loadStarted = Clock::now();
        auto pipelineResult = pek::runtime::Pipeline::fromString(description);
        if (!pipelineResult)
            throw std::runtime_error(pipelineResult.error().toString());
        auto pipeline = std::move(*pipelineResult);
        const double loadMs = elapsedMs(loadStarted, Clock::now());

        std::mutex timingMutex;
        std::vector<Clock::time_point> resultReadyTimes;
        pipeline.onFrameResults([&](const std::string &) {
            const auto ready = Clock::now();
            std::lock_guard lock(timingMutex);
            resultReadyTimes.push_back(ready);
        });

        auto startResult = pipeline.start();
        if (!startResult)
            throw std::runtime_error(startResult.error().toString());
        auto waitResult = pipeline.wait();
        auto stopResult = pipeline.stop();
        if (!waitResult)
            throw std::runtime_error(waitResult.error().toString());
        if (!stopResult)
            throw std::runtime_error(stopResult.error().toString());

        std::lock_guard lock(timingMutex);
        if (resultReadyTimes.size() != expectedFrames) {
            throw std::runtime_error(
                fmt::format("Received {} serialized inference results for the measured video "
                            "pass; expected exactly {}",
                            resultReadyTimes.size(),
                            expectedFrames));
        }
        const auto measuredFrames = expectedFrames - WARMUP_FRAMES;
        const double measurementMs = elapsedMs(resultReadyTimes.front(), resultReadyTimes.back());
        if (measurementMs <= 0.0)
            throw std::runtime_error("Measured video interval must be positive");
        const double pipelineFps = static_cast<double>(measuredFrames) * 1000.0 / measurementMs;

        nlohmann::json summary = {
            {"schema", SCHEMA_ID},
            {"runner", "pek-pipeline-video"},
            {"measurement",
             {{"technique", "preloaded_video_result_intervals"},
              {"timed_region", "first_serialized_result_ready_to_last_serialized_result_ready"},
              {"decode_included", false},
              {"source_color_conversion_included", false},
              {"preloaded_frames", true},
              {"artifact_write_excluded", true},
              {"video_pacing_disabled", true},
              {"warmup_video_passes", 1},
              {"warmup_frames", WARMUP_FRAMES}}},
            {"inputs",
             {{"model", ""},
              {"opchain", args.at("--opchain")},
              {"video", args.at("--video")},
              {"video_sha256", source.at("sha256")},
              {"source_width", source.at("width")},
              {"source_height", source.at("height")},
              {"source_fps", source.at("fps")},
              {"source_frame_count", expectedFrames},
              {"imgsz", IMG_SIZE},
              {"device", "cpu"}}},
            {"timing",
             {{"load_ms", loadMs},
              {"total_frames", expectedFrames},
              {"measured_frames", measuredFrames},
              {"elapsed_ms", measurementMs},
              {"pipeline_fps", pipelineFps}}},
        };

        const std::filesystem::path summaryPath = args.at("--summary");
        if (summaryPath.has_parent_path())
            std::filesystem::create_directories(summaryPath.parent_path());
        std::ofstream output(summaryPath);
        if (!output)
            throw std::runtime_error("Failed to write summary: " + summaryPath.string());
        output << summary.dump(2) << '\n';
        fmt::print("PEK video FPS: {:.3f}\n", pipelineFps);
        return 0;
    } catch (const std::exception &error) {
        fmt::print(stderr, "yolo-video-benchmark: {}\n", error.what());
        return 1;
    }
}
