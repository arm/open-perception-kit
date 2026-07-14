/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "parser.hpp"
#include "pek/Log.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

struct PipelineEntry {
    std::string full_path{};   // filename with extension, internally used as ID
    std::string description{}; // The description of the pipeline from the JSON file
    std::string pipeline{};    // The pipeline definition from the JSON file
};

static constexpr const char *kPipelinesDir = "/work/config/pipelines";
static constexpr const char *kLastSelectionFileName = ".last_selected_pipeline_id";

static fs::path last_selection_path() {
    return fs::path(kPipelinesDir) / kLastSelectionFileName;
}

static std::optional<std::string> load_last_selected_pipeline() {
    std::ifstream in(last_selection_path());
    if (!in.is_open())
        return std::nullopt;

    std::string id;
    std::getline(in, id);
    id = trim(id);
    if (id.empty())
        return std::nullopt;
    return id;
}

static bool save_last_selected_pipeline(const std::string &pipeline) {
    std::ofstream out(last_selection_path(), std::ios::trunc);
    if (!out.is_open())
        return false;
    out << pipeline << "\n";
    return true;
}

static std::optional<PipelineEntry> load_entry_from_json_file(const fs::path &p) {
    try {
        std::ifstream in(p);
        if (!in.is_open()) {
            pek::forceLoge("Failed to open: {}\n", p.string());
            return std::nullopt;
        }

        json json_content;
        in >> json_content;

        if (!json_content.contains("description") || !json_content["description"].is_string()) {
            pek::forceLoge("Invalid JSON (missing string 'description'): {}\n", p.string());
            return std::nullopt;
        }
        if (!json_content.contains("pipeline")) {
            pek::forceLoge("Invalid JSON (missing 'pipeline'): {}\n", p.string());
            return std::nullopt;
        }

        PipelineEntry pipeline_entry{};
        pipeline_entry.full_path = p.string();
        pipeline_entry.description = json_content["description"].get<std::string>();

        const auto &pipeline_elements = json_content["pipeline"];
        if (pipeline_elements.is_string()) {
            pipeline_entry.pipeline = pipeline_elements.get<std::string>();
        } else if (pipeline_elements.is_array()) {
            std::string joined;
            bool first = true;

            for (const auto &element : pipeline_elements) {
                if (!element.is_string()) {
                    pek::forceLoge(
                        "Invalid JSON ('pipeline' array must contain only strings): {}\n",
                        p.string());
                    return std::nullopt;
                }
                const std::string part = element.get<std::string>();
                if (part.empty())
                    continue;

                if (!first)
                    joined.push_back(' ');
                joined += part;
                first = false;
            }

            if (joined.empty()) {
                pek::forceLoge("Invalid JSON ('pipeline' array is empty after joining): {}\n",
                               p.string());
                return std::nullopt;
            }

            pipeline_entry.pipeline = std::move(joined);
        } else {
            pek::forceLoge("Invalid JSON ('pipeline' must be a string or array of strings): {}\n",
                           p.string());
            return std::nullopt;
        }

        return pipeline_entry;
    } catch (const std::exception &ex) {
        pek::forceLoge("JSON parse error in {}: {}\n", p.string(), ex.what());
        return std::nullopt;
    }
}

static std::vector<PipelineEntry> enumerate_entries() {
    std::vector<PipelineEntry> entries;
    const fs::path pipelines_directory(kPipelinesDir);

    std::error_code ec;
    if (!fs::exists(pipelines_directory, ec) || !fs::is_directory(pipelines_directory, ec)) {
        pek::forceLoge("Directory not found or not a directory: {}\n",
                       pipelines_directory.string());
        return entries;
    }

    for (const auto &fileSystemObject : fs::directory_iterator(pipelines_directory, ec)) {
        if (ec)
            break;
        if (!fileSystemObject.is_regular_file())
            continue;

        const auto &p = fileSystemObject.path();
        if (p.extension() != ".json")
            continue;

        auto opt = load_entry_from_json_file(p);
        if (opt) {
            entries.push_back(*opt);
        }
    }

    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
        return a.full_path < b.full_path;
    });

    return entries;
}
static bool file_exists(const fs::path &p) {
    std::error_code ec;
    const bool exists = fs::exists(p, ec);
    if (ec || !exists)
        return false;
    return fs::is_regular_file(p, ec) && !ec;
};

// Resolves a pipeline argument which can be either:
// - A full path to a JSON file
// - A pipeline ID/stem (resolved against kPipelinesDir with/without .json extension)
static std::optional<std::string> resolve_pipeline_path(const std::string &requested) {

    const fs::path req_path(requested);

    // First, check if it's already a valid full path
    if (file_exists(req_path)) {
        return requested;
    }

    // Try to resolve as an ID in kPipelinesDir:
    // 1. <kPipelinesDir>/<requested>.json
    // 2. <kPipelinesDir>/<requested>
    fs::path candidate1 = fs::path(kPipelinesDir) / (requested + ".json");
    if (file_exists(candidate1)) {
        return candidate1.string();
    }

    fs::path candidate2 = fs::path(kPipelinesDir) / requested;
    if (file_exists(candidate2)) {
        return candidate2.string();
    }

    return std::nullopt;
}

static std::optional<int> read_choice_int() {
    std::string line;
    if (!std::getline(std::cin, line))
        return std::nullopt;
    line = trim(line);
    if (line.empty())
        return std::nullopt;

    char *end = nullptr;
    errno = 0;
    long v = std::strtol(line.c_str(), &end, 10);
    if (errno != 0 || end == line.c_str() || *end != '\0')
        return std::nullopt;
    if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max())
        return std::nullopt;
    return static_cast<int>(v);
}

int run_gst_launch(const std::string &pipeline, bool dry_run) {
    try {
        auto cmd = tokenize_and_expand_argv(pipeline);

        std::string command_line;
        for (auto &p : cmd.storage) {
            command_line += p;
            command_line += ' ';
        }
        pek::forceLog("{}\n", command_line);
        std::fflush(stdout);

        if (!dry_run) {
            // never returns if everything is okay
            execvp(cmd.argv[0], cmd.argv.data());
            pek::forceLoge("execvp: {}\n", std::strerror(errno));
            return 127;
        }

        return 0;
    } catch (std::runtime_error &error) {
        pek::forceLoge("error: {}\n", error.what());
        return 3;
    }
}
// clang-format off
static void print_usage(const char *argv0) {
    pek::forceLog(
        "Usage:\n"
        "  {}              # show menu\n"
        "  {} -h           # print this help\n"
        "  {} -l           # run last selected pipeline\n"
        "  {} -p           # dry run (only prints the pipeline without executing it)\n"
        "  {} <pipeline>   # run pipeline by ID (e.g., 'onnx') or full path to a JSON file. Shall not be used together with -l\n"
        "\n"
        "Environment:\n"
        "  OPK_LOG_LEVEL=0..4   # log verbosity: 0=off, 1=errors, 2=warnings, 3=notices, 4=info (default: 4)\n",
        argv0,
        argv0,
        argv0,
        argv0,
        argv0);
}
// clang-format on

static int parse_args(int argc,
                      char **argv,
                      bool &run_last,
                      bool &dry_run,
                      std::optional<std::string> &requested_pipeline) {
    requested_pipeline.reset();

    opterr = 0; // we'll print our own usage

    int opt;
    while ((opt = getopt(argc, argv, ":lph")) != -1) {
        switch (opt) {
        case 'l':
            run_last = true;
            break;
        case 'p':
            dry_run = true;
            break;
        case 'h':
        default:
            print_usage(argv[0]);
            return 2;
        }
    }

    // Remaining args after options are positional (the <pipeline>)
    const int remaining = argc - optind;

    if (remaining == 0) {
        // OK: menu mode OR -l OR -p OR -l -p
    } else if (remaining == 1) {
        requested_pipeline = argv[optind];
    } else {
        print_usage(argv[0]);
        return 2;
    }

    // Enforce: -l and <pipeline> cannot coexist
    if (run_last && requested_pipeline.has_value()) {
        print_usage(argv[0]);
        return 2;
    }

    return 0;
}

int main(int argc, char **argv) {
    // Parse XOR args: "-l" OR "<pipeline>" OR none
    bool run_last = false;
    bool dry_run = false;
    std::optional<std::string> requested_pipeline;

    auto rc = parse_args(argc, argv, run_last, dry_run, requested_pipeline);
    if (rc != 0)
        return rc;

    auto entries = enumerate_entries();
    if (entries.empty()) {
        pek::forceLog("No valid pipelines found in: {}\n", kPipelinesDir);
        return 1;
    }

    std::unordered_map<std::string, size_t> id_to_idx;
    id_to_idx.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        id_to_idx[entries[i].full_path] = i;
    }

    // Fast path: run last
    if (run_last) {
        auto last_pipeline = load_last_selected_pipeline();
        if (!last_pipeline) {
            pek::forceLog("No previous selection stored ({}).\n", last_selection_path().string());
            return 3;
        }
        auto it = id_to_idx.find(*last_pipeline);
        if (it == id_to_idx.end()) {
            pek::forceLog("Last selected pipeline '{}' not found in directory.\n", *last_pipeline);
            return 3;
        }
        const auto &pipelineEntry = entries[it->second];
        (void)save_last_selected_pipeline(pipelineEntry.full_path);
        return run_gst_launch(pipelineEntry.pipeline, dry_run);
    }

    // Fast path: run by path or ID
    if (requested_pipeline) {
        auto resolved = resolve_pipeline_path(*requested_pipeline);
        if (!resolved) {
            pek::forceLog("Pipeline not found: '{}' (expected full path or ID in {})\n",
                          *requested_pipeline,
                          kPipelinesDir);
            return 3;
        }
        auto entry = load_entry_from_json_file(*resolved);
        if (!entry) {
            pek::forceLog("Failed to load pipeline from: {}\n", *resolved);
            return 3;
        }
        // Since this path might not be available in the menu, we won't save it as last selected
        // pipeline.
        return run_gst_launch(entry->pipeline, dry_run);
    }

    // Menu mode (no args)
    auto last_pipeline = load_last_selected_pipeline();
    std::optional<size_t> last_pipeline_idx;
    if (last_pipeline) {
        auto it = id_to_idx.find(*last_pipeline);
        if (it != id_to_idx.end())
            last_pipeline_idx = it->second;
    }

    pek::forceLog("Pipelines in: {}\n", kPipelinesDir);
    if (last_pipeline_idx) {
        const auto &pipelineEntry = entries[*last_pipeline_idx];
        pek::forceLog("0 -> {} [LAST: {}]\n", pipelineEntry.full_path, pipelineEntry.description);
    } else {
        pek::forceLog("0 -> (no previous selection)\n");
    }

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto &pipelineEntry = entries[i];
        pek::forceLog("{} -> {} [{}]\n", i + 1, pipelineEntry.full_path, pipelineEntry.description);
    }

    const int max_choice = static_cast<int>(entries.size());
    while (true) {
        pek::forceLog("\nSelect (0..{}): ", max_choice);
        std::fflush(stdout);
        auto c = read_choice_int();
        if (!c || *c < 0 || *c > max_choice) {
            pek::forceLoge("Invalid choice. Try again.\n");
            continue;
        }

        if (*c == 0) {
            if (!last_pipeline_idx) {
                pek::forceLog("No previous selection stored. Choose 1..{}.\n", max_choice);
                continue;
            }
            const auto &pipelineEntry = entries[*last_pipeline_idx];
            (void)save_last_selected_pipeline(pipelineEntry.full_path);
            return run_gst_launch(pipelineEntry.pipeline, dry_run);
        }

        const size_t idx = static_cast<size_t>(*c - 1);
        const auto &pipelineEntry = entries[idx];

        if (!save_last_selected_pipeline(pipelineEntry.full_path)) {
            pek::forceLog("Warning: failed to save last selected pipeline to {}\n",
                          last_selection_path().string());
        }
        return run_gst_launch(pipelineEntry.pipeline, dry_run);
    }
}
