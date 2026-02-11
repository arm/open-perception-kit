
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
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
#include <wordexp.h>

#include <nlohmann/json.hpp>

#include "parsar.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

struct PipelineEntry {
    std::string id;          // filename without .json
    std::string description; // from json
    std::string pipeline;    // from json
};

static constexpr const char *kPipelinesDir = "/work/scripts/pipelines"; // <-- hardcode here
static constexpr const char *kLastSelectionFileName = ".last_selected_pipeline_id";

static std::string trim(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

static fs::path last_selection_path() {
    return fs::path(kPipelinesDir) / kLastSelectionFileName;
}

static std::optional<std::string> load_last_selected_id() {
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

static bool save_last_selected_id(const std::string &id) {
    std::ofstream out(last_selection_path(), std::ios::trunc);
    if (!out.is_open())
        return false;
    out << id << "\n";
    return true;
}

static std::optional<PipelineEntry> load_entry_from_json_file(const fs::path &p) {
    try {
        std::ifstream in(p);
        if (!in.is_open()) {
            std::cerr << "Failed to open: " << p << "\n";
            return std::nullopt;
        }

        json j;
        in >> j;

        if (!j.contains("description") || !j["description"].is_string()) {
            std::cerr << "Invalid JSON (missing string 'description'): " << p << "\n";
            return std::nullopt;
        }
        if (!j.contains("pipeline")) {
            std::cerr << "Invalid JSON (missing 'pipeline'): " << p << "\n";
            return std::nullopt;
        }

        PipelineEntry e;
        e.id = p.stem().string();
        e.description = j["description"].get<std::string>();

        const auto &jp = j["pipeline"];
        if (jp.is_string()) {
            e.pipeline = jp.get<std::string>();
        } else if (jp.is_array()) {
            std::string joined;
            bool first = true;

            for (const auto &item : jp) {
                if (!item.is_string()) {
                    std::cerr << "Invalid JSON ('pipeline' array must contain only strings): " << p
                              << "\n";
                    return std::nullopt;
                }
                const std::string part = item.get<std::string>();
                if (part.empty())
                    continue;

                if (!first)
                    joined.push_back(' ');
                joined += part;
                first = false;
            }

            if (joined.empty()) {
                std::cerr << "Invalid JSON ('pipeline' array is empty after joining): " << p
                          << "\n";
                return std::nullopt;
            }

            e.pipeline = std::move(joined);
        } else {
            std::cerr << "Invalid JSON ('pipeline' must be a string or array of strings): " << p
                      << "\n";
            return std::nullopt;
        }

        return e;
    } catch (const std::exception &ex) {
        std::cerr << "JSON parse error in " << p << ": " << ex.what() << "\n";
        return std::nullopt;
    }
}

static std::vector<PipelineEntry> enumerate_entries() {
    std::vector<PipelineEntry> entries;
    const fs::path dir(kPipelinesDir);

    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        std::cerr << "Directory not found or not a directory: " << dir << "\n";
        return entries;
    }

    for (const auto &de : fs::directory_iterator(dir, ec)) {
        if (ec)
            break;
        if (!de.is_regular_file())
            continue;

        const auto &p = de.path();
        if (p.extension() != ".json")
            continue;

        auto opt = load_entry_from_json_file(p);
        if (opt)
            entries.push_back(*opt);
    }

    std::sort(
        entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.id < b.id; });

    return entries;
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

// Execvp from a vector<string>. On success this does not return.
static int exec_from_args(const std::vector<std::string> &args) {
    if (args.empty()) {
        std::cerr << "No args\n";
        return 1;
    }
    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    // duplicate strings into heap (mutable C-strings)
    std::vector<char *> allocated;
    allocated.reserve(args.size());
    for (const auto &s : args) {
        char *c = strdup(s.c_str());
        if (!c) {
            perror("strdup");
            for (char *p : allocated)
                free(p);
            return 127;
        }
        allocated.push_back(c);
        argv.push_back(c);
    }
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    // if execvp returned, it failed
    perror("execvp");
    for (char *p : allocated)
        free(p);
    return 127;
}

int run_gst_launch(const std::string &pipeline, bool dry_run) {
    try {
        auto cmd = tokenize_and_expand_argv(pipeline);

        for (auto &p : cmd.storage) {
            std::cout << p << " ";
        }
        std::cout << std::endl;

        if (!dry_run) {
            // never returns if everything is okay
            execvp(cmd.argv[0], cmd.argv.data());
            perror("execvp");
            return 127;
        }

        return 0;
    } catch (std::runtime_error &e) {
        std::cerr << "error: " << e.what() << std::endl;
        return 3;
    }
}

static void print_usage(const char *argv0) {
    std::cerr << "Usage:\n"
              << "  " << argv0 << "              # show menu\n"
              << "  " << argv0 << " -h           # print this help\n"
              << "  " << argv0 << " -l           # run last selected pipeline\n"
              << "  " << argv0
              << " -p           # dry run (only prints the pipeline without executing it)\n"
              << "  " << argv0 << " <id>         # run pipeline by id (filename without .json)\n";
}

static int parse_args(int argc,
                      char **argv,
                      bool &run_last,
                      bool &dry_run,
                      std::optional<std::string> &requested_id) {
    run_last = false;
    dry_run = false;
    requested_id.reset();

    opterr = 0; // we'll print our own usage

    int opt;
    while ((opt = getopt(argc, argv, "lph")) != -1) {
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

    // Remaining args after options are positional (the <id>)
    const int remaining = argc - optind;

    if (remaining == 0) {
        // OK: menu mode OR -l OR -p OR -l -p
    } else if (remaining == 1) {
        requested_id = argv[optind];
    } else {
        print_usage(argv[0]);
        return 2;
    }

    // Enforce: -l and <id> cannot coexist
    if (run_last && requested_id.has_value()) {
        print_usage(argv[0]);
        return 2;
    }

    return 0;
}

int main(int argc, char **argv) {
    // Parse XOR args: "-l" OR "<id>" OR none
    bool run_last = false;
    bool dry_run = false;
    std::optional<std::string> requested_id;

    auto rc = parse_args(argc, argv, run_last, dry_run, requested_id);
    if (rc != 0)
        return rc;

    auto entries = enumerate_entries();
    if (entries.empty()) {
        std::cerr << "No valid pipelines found in: " << kPipelinesDir << "\n";
        return 1;
    }

    std::unordered_map<std::string, size_t> id_to_idx;
    id_to_idx.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        id_to_idx[entries[i].id] = i;
    }

    // Fast path: run last
    if (run_last) {
        auto last_id = load_last_selected_id();
        if (!last_id) {
            std::cerr << "No previous selection stored (" << last_selection_path() << ").\n";
            return 3;
        }
        auto it = id_to_idx.find(*last_id);
        if (it == id_to_idx.end()) {
            std::cerr << "Last selected id '" << *last_id << "' not found in directory.\n";
            return 3;
        }
        const auto &e = entries[it->second];
        (void)save_last_selected_id(e.id);
        return run_gst_launch(e.pipeline, dry_run);
    }

    // Fast path: run by id
    if (requested_id) {
        auto it = id_to_idx.find(*requested_id);
        if (it == id_to_idx.end()) {
            std::cerr << "Unknown id '" << *requested_id << "'. Available ids:\n";
            for (const auto &e : entries)
                std::cerr << "  " << e.id << "\n";
            return 4;
        }
        const auto &e = entries[it->second];
        if (!save_last_selected_id(e.id)) {
            std::cerr << "Warning: failed to save last selected id to " << last_selection_path()
                      << "\n";
        }
        return run_gst_launch(e.pipeline, dry_run);
    }

    // Menu mode (no args)
    auto last_id = load_last_selected_id();
    std::optional<size_t> last_idx;
    if (last_id) {
        auto it = id_to_idx.find(*last_id);
        if (it != id_to_idx.end())
            last_idx = it->second;
    }

    std::cout << "Pipelines in: " << kPipelinesDir << "\n";
    if (last_idx) {
        const auto &e = entries[*last_idx];
        std::cout << "0 -> " << e.id << " [LAST: " << e.description << "]\n";
    } else {
        std::cout << "0 -> (no previous selection)\n";
    }

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto &e = entries[i];
        std::cout << (i + 1) << " -> " << e.id << " [" << e.description << "]\n";
    }

    const int max_choice = static_cast<int>(entries.size());
    while (true) {
        std::cout << "\nSelect (0.." << max_choice << "): " << std::flush;
        auto c = read_choice_int();
        if (!c || *c < 0 || *c > max_choice) {
            std::cout << "Invalid choice. Try again.\n";
            continue;
        }

        if (*c == 0) {
            if (!last_idx) {
                std::cout << "No previous selection stored. Choose 1.." << max_choice << ".\n";
                continue;
            }
            const auto &e = entries[*last_idx];
            (void)save_last_selected_id(e.id);
            return run_gst_launch(e.pipeline, dry_run);
        }

        const size_t idx = static_cast<size_t>(*c - 1);
        const auto &e = entries[idx];

        if (!save_last_selected_id(e.id)) {
            std::cerr << "Warning: failed to save last selected id to " << last_selection_path()
                      << "\n";
        }
        return run_gst_launch(e.pipeline, dry_run);
    }
}
