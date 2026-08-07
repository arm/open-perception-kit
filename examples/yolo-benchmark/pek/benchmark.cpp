/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "runtime/OpChain.h"
#include "runtime/Tools.h"
#include "runtime/VideoFrame.h"

#include <fmt/core.h>
#include <nlohmann/json.hpp>
#include <perf/PerformanceTracer.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
const std::string FINGERPRINT_HEADER = "# image_set_fingerprint=";
constexpr const char *SCHEMA_ID = "expkits_yolo_benchmark.v1";

struct ImageRow {
    std::string imageId;
    std::string imagePath;
};

struct ImageList {
    std::string fingerprint;
    std::vector<ImageRow> rows;
};

struct PreloadedImage {
    std::string imageId;
    std::string imagePath;
    pek::runtime::VideoFrame frame;
};

struct StageTimes {
    double preprocessMs = 0.0;
    double inferenceMs = 0.0;
    double postprocessMs = 0.0;
};

void printUsage(const char *programName) {
    fmt::print(stderr,
               "Usage: {} --opchain <opchain.json> --images <images.tsv> "
               "--output <predictions.jsonl> --summary <benchmark_summary.json>\n",
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

double elapsedMs(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

double percentile(const std::vector<double> &sortedValues, double q) {
    if (sortedValues.empty())
        return 0.0;
    const auto index =
        static_cast<std::size_t>(std::ceil(q * static_cast<double>(sortedValues.size())) - 1.0);
    return sortedValues[std::min(index, sortedValues.size() - 1)];
}

nlohmann::json timingStats(std::vector<double> values) {
    nlohmann::json stats;
    stats["count"] = values.size();
    if (values.empty()) {
        stats["avg_ms"] = 0.0;
        stats["p50_ms"] = 0.0;
        stats["p75_ms"] = 0.0;
        stats["p95_ms"] = 0.0;
        stats["p99_ms"] = 0.0;
        stats["min_ms"] = 0.0;
        stats["max_ms"] = 0.0;
        return stats;
    }

    std::sort(values.begin(), values.end());
    const double sum = std::accumulate(values.begin(), values.end(), 0.0);
    stats["avg_ms"] = sum / static_cast<double>(values.size());
    stats["p50_ms"] = percentile(values, 0.50);
    stats["p75_ms"] = percentile(values, 0.75);
    stats["p95_ms"] = percentile(values, 0.95);
    stats["p99_ms"] = percentile(values, 0.99);
    stats["min_ms"] = values.front();
    stats["max_ms"] = values.back();
    return stats;
}

ImageList readImageList(const std::string &imagesPath) {
    std::ifstream images(imagesPath);
    if (!images)
        throw std::runtime_error("Failed to open image list: " + imagesPath);

    ImageList result;
    std::string line;
    while (std::getline(images, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind(FINGERPRINT_HEADER, 0) == 0) {
            result.fingerprint = line.substr(FINGERPRINT_HEADER.size());
            continue;
        }
        if (line.empty() || line[0] == '#')
            continue;

        const auto tab = line.find('\t');
        if (tab == std::string::npos)
            throw std::runtime_error(imagesPath + ": expected image_id<TAB>image_path");
        const std::string imageId = line.substr(0, tab);
        const std::string imagePath = line.substr(tab + 1);
        result.rows.push_back({imageId, imagePath});
    }
    if (result.fingerprint.empty())
        throw std::runtime_error(imagesPath + ": missing " + FINGERPRINT_HEADER +
                                 " header; use prepare_dataset.py");
    return result;
}

pek::runtime::Result<pek::runtime::VideoFrame> loadFrame(const std::string &imagePath) {
    std::size_t width = 0;
    std::size_t height = 0;
    auto bgraPixels = pek::runtime::Tools::loadImageFileBgra(imagePath, width, height);
    if (!bgraPixels) {
        return tl::make_unexpected(bgraPixels.error());
    }

    auto frame = pek::runtime::VideoFrame::moveBgra(std::move(*bgraPixels), width, height);
    if (!frame) {
        return tl::make_unexpected(frame.error());
    }

    return std::move(*frame);
}

pek::runtime::Result<std::string> runFrame(pek::runtime::OpChain &opChain,
                                           const pek::runtime::VideoFrame &frame,
                                           const std::string &inferId) {
    return opChain.run(frame, inferId);
}

nlohmann::json resultJson(const ImageRow &row, const std::string &perceptionJson) {
    const auto perception = nlohmann::json::parse(perceptionJson);
    nlohmann::json detections = nlohmann::json::array();

    for (const auto &layer : perception.value("layers", nlohmann::json::array())) {
        if (layer.value("contentType", "") != "genericObject")
            continue;

        for (const auto &detection : layer.value("detections", nlohmann::json::array())) {
            if (detection.value("type", "") != "Rect" || !detection.contains("data"))
                continue;

            const auto &data = detection["data"];
            const double x = data.value("x", 0.0);
            const double y = data.value("y", 0.0);
            const double width = data.value("width", 0.0);
            const double height = data.value("height", 0.0);

            nlohmann::json item;
            const std::string className = data.value("text", "");
            item["bbox_xyxy"] = nlohmann::json::array({x, y, x + width, y + height});
            item["class_id"] = data.value("classId", -1);
            item["class_name"] = className;
            item["confidence"] = data.value("confidence", 0.0);
            detections.push_back(std::move(item));
        }
    }

    nlohmann::json outputRow;
    outputRow["image_id"] = row.imageId;
    outputRow["image_path"] = row.imagePath;
    outputRow["detections"] = std::move(detections);
    return outputRow;
}

void ensureParentDirectory(const std::string &path) {
    const std::filesystem::path file(path);
    if (file.has_parent_path())
        std::filesystem::create_directories(file.parent_path());
}

std::string timingsPathFor(const std::string &outputPath) {
    std::filesystem::path path(outputPath);
    path.replace_filename("timings.jsonl");
    return path.string();
}

StageTimes stageTimesFromTracer(const pek::perf::PerformanceTracer &tracer) {
    StageTimes times;
    for (const auto &measurement : tracer.getCurrentCycleMeasurements()) {
        const auto &key = measurement.key;
        if (key.find("/GenImgPre/") != std::string::npos) {
            times.preprocessMs += measurement.duration_ms();
        } else if (key.find("/Infer/") != std::string::npos) {
            times.inferenceMs += measurement.duration_ms();
        } else if (key.find("/Post/") != std::string::npos) {
            times.postprocessMs += measurement.duration_ms();
        }
    }
    return times;
}

void writeTimingRows(const std::string &timingsPath,
                     const std::vector<ImageRow> &rows,
                     const std::vector<PreloadedImage> &preloadedRows,
                     const std::vector<double> &imageTimesMs,
                     const std::vector<StageTimes> &stageTimes,
                     const std::vector<std::size_t> &detectionCounts) {
    ensureParentDirectory(timingsPath);
    std::ofstream timings(timingsPath);
    if (!timings)
        throw std::runtime_error("Failed to open timing file: " + timingsPath);

    for (std::size_t i = 0; i < rows.size(); ++i) {
        nlohmann::json row;
        row["image_index"] = i + 1;
        row["image_id"] = rows[i].imageId;
        row["image_path"] = rows[i].imagePath;
        row["width"] = preloadedRows[i].frame.width();
        row["height"] = preloadedRows[i].frame.height();
        row["detections"] = detectionCounts[i];
        row["wall_ms"] = imageTimesMs[i];
        row["preprocess_ms"] = stageTimes[i].preprocessMs;
        row["inference_ms"] = stageTimes[i].inferenceMs;
        row["postprocess_ms"] = stageTimes[i].postprocessMs;
        timings << row.dump() << '\n';
    }
}

int runBenchmark(const std::string &opchainPath,
                 const std::string &imagesPath,
                 const std::string &outputPath,
                 const std::string &summaryPath) {
    const auto loadStarted = Clock::now();
    auto opChain = pek::runtime::OpChain::fromJsonFile(opchainPath);
    const double loadMs = elapsedMs(loadStarted, Clock::now());
    if (!opChain) {
        fmt::print(stderr, "{}\n", opChain.error().toString());
        return 1;
    }

    ImageList imageList;
    try {
        imageList = readImageList(imagesPath);
    } catch (const std::exception &error) {
        fmt::print(stderr, "{}\n", error.what());
        return 1;
    }
    const auto &rows = imageList.rows;

    std::vector<PreloadedImage> preloadedRows;
    preloadedRows.reserve(rows.size());
    const auto preloadStarted = Clock::now();
    for (const auto &row : rows) {
        auto frame = loadFrame(row.imagePath);
        if (!frame) {
            fmt::print(stderr, "{}: {}\n", row.imagePath, frame.error().toString());
            return 1;
        }
        preloadedRows.push_back({row.imageId, row.imagePath, std::move(*frame)});
    }
    const double preloadMs = elapsedMs(preloadStarted, Clock::now());

    const std::size_t warmupImages = std::min<std::size_t>(1, rows.size());
    auto *tracer = pek::perf::getGlobalTracer();
    for (std::size_t i = 0; i < warmupImages; ++i) {
        auto warmupResult =
            runFrame(*opChain, preloadedRows[i].frame, "warmup_" + preloadedRows[i].imageId);
        if (!warmupResult) {
            fmt::print(stderr, "{}: {}\n", rows[i].imagePath, warmupResult.error().toString());
            return 1;
        }
    }
    tracer->reset();

    ensureParentDirectory(outputPath);
    std::ofstream output(outputPath);
    if (!output) {
        fmt::print(stderr, "Failed to open output file: {}\n", outputPath);
        return 1;
    }

    std::vector<double> imageTimesMs(rows.size(), 0.0);
    std::vector<StageTimes> stageTimes(rows.size());
    std::vector<std::size_t> detectionCounts(rows.size(), 0);
    const auto loopStarted = Clock::now();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        tracer->reset();
        const auto imageStarted = Clock::now();
        auto perceptionJson = runFrame(*opChain, preloadedRows[i].frame, rows[i].imageId);
        const auto imageFinished = Clock::now();
        if (!perceptionJson) {
            fmt::print(stderr, "{}: {}\n", rows[i].imagePath, perceptionJson.error().toString());
            return 1;
        }

        imageTimesMs[i] = elapsedMs(imageStarted, imageFinished);
        stageTimes[i] = stageTimesFromTracer(*tracer);
        const auto prediction = resultJson(rows[i], *perceptionJson);
        detectionCounts[i] = prediction["detections"].size();
        output << prediction.dump() << '\n';
    }
    const double loopWallMs = elapsedMs(loopStarted, Clock::now());
    const std::string timingsPath = timingsPathFor(outputPath);
    try {
        writeTimingRows(
            timingsPath, rows, preloadedRows, imageTimesMs, stageTimes, detectionCounts);
    } catch (const std::exception &error) {
        fmt::print(stderr, "{}\n", error.what());
        return 1;
    }

    ensureParentDirectory(summaryPath);
    std::ofstream summary(summaryPath);
    if (!summary) {
        fmt::print(stderr, "Failed to open summary file: {}\n", summaryPath);
        return 1;
    }

    nlohmann::json doc;
    doc["schema"] = SCHEMA_ID;
    doc["runner"] = "pek-opchain";
    doc["measurement"] = {
        {"technique", "in_process_image_loop_wall_clock"},
        {"timed_region", "preloaded_image_to_postprocess_result_ready"},
        {"model_load_timed_separately", true},
        {"output_serialization_excluded_from_per_image", true},
        {"preloaded_images", true},
        {"warmup_images", warmupImages},
    };
    doc["inputs"] = {
        {"model", ""},
        {"opchain", opchainPath},
        {"image_list", imagesPath},
        {"image_count", rows.size()},
        {"image_set_fingerprint", imageList.fingerprint},
        {"imgsz", 320},
        {"device", "cpu"},
    };
    doc["outputs"] = {
        {"predictions_jsonl", outputPath},
        {"timings_jsonl", timingsPath},
    };
    doc["timing"] = {
        {"load_ms", loadMs},
        {"preload_ms", preloadMs},
        {"loop_wall_ms", loopWallMs},
        {"per_image_ms", timingStats(imageTimesMs)},
    };
    summary << doc.dump(2) << '\n';

    fmt::print(stderr, "processed {} images\n", rows.size());
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    const auto args = parseFlagArgs(argc, argv);
    if (!args.count("--opchain") || !args.count("--images") || !args.count("--output") ||
        !args.count("--summary")) {
        printUsage(argv[0]);
        return 2;
    }

    return runBenchmark(
        args.at("--opchain"), args.at("--images"), args.at("--output"), args.at("--summary"));
}
