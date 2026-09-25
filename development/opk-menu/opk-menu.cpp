/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <getopt.h>
#include <mutex>
#include <optional>
#include <poll.h>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Log.h"
#include "discover.hpp"
#include "menu.hpp"
#include "opk/FrameResults.h"
#include "opk/PipelinePreset.h"
#include "opk/String.h"
#include "runtime/Pipeline.h"

namespace fs = std::filesystem;

struct PipelineEntry {
    std::string id{};          // filename with extension, independent of the project root
    std::string description{}; // The description of the pipeline from the JSON file
    std::string sourceInfo{};  // Short source requirement shown by opk-menu
    std::string pipeline{};    // The pipeline definition from the JSON file
    bool loop{};               // Seek finite media back to the beginning after each segment.
};

using PipelineIndex = std::unordered_map<std::string, size_t>;

static constexpr const char *kDefaultProjectRoot = "/work";
static constexpr const char *kLastSelectionFileName = ".last_selected_pipeline_id";
static constexpr int kExecutionFailureExitCode = 127;
static constexpr int kSignalExitCodeOffset = 128;
static constexpr int kBackToMenuCode = -1;
static constexpr std::string_view kAnsiReset = "\x1b[0m";
static constexpr std::string_view kAnsiPipeline = "\x1b[1;35m";
static constexpr std::string_view kAnsiPresentFile = "\x1b[30;42m";
static constexpr std::string_view kAnsiMissingFile = "\x1b[30;41m";
static constexpr std::string_view kAnsiReverse = "\x1b[7m";
static constexpr auto kProgressRenderInterval = std::chrono::milliseconds(250);
static volatile sig_atomic_t requested_termination_signal = 0;

struct PluginPath {
    std::string path;
    bool required = false;
};

static fs::path pipelines_directory() {
    const char *configured_root = std::getenv("OPK_PROJECT_ROOT");
    const fs::path project_root = configured_root != nullptr && configured_root[0] != '\0'
                                      ? configured_root
                                      : kDefaultProjectRoot;
    return project_root / "config" / "pipelines";
}

static PluginPath plugin_path() {
    if (const char *configured_path = std::getenv("OPK_PLUGIN_PATH")) {
        if (configured_path[0] != '\0')
            return {configured_path, true};
    }

    const char *configured_root = std::getenv("OPK_PROJECT_ROOT");
    const fs::path project_root = configured_root != nullptr && configured_root[0] != '\0'
                                      ? configured_root
                                      : kDefaultProjectRoot;
    return {
        .path = (project_root / "development/build-active/meson-out").string(),
        .required = false,
    };
}

static bool file_exists(const fs::path &p) {
    std::error_code ec;
    const bool exists = fs::exists(p, ec);
    if (ec || !exists)
        return false;
    return fs::is_regular_file(p, ec) && !ec;
}

static bool progress_status_enabled() {
    return isatty(STDERR_FILENO) != 0;
}

static bool pipeline_back_key_enabled() {
    return isatty(STDIN_FILENO) != 0;
}

static bool terminal_style_enabled() {
    const char *no_color = std::getenv("NO_COLOR");
    return no_color == nullptr || no_color[0] == '\0';
}

static bool stdout_style_enabled() {
    return terminal_style_enabled() && isatty(STDOUT_FILENO) != 0;
}

static void print_expanded_pipeline(std::string_view expanded_pipeline) {
    if (stdout_style_enabled()) {
        opk::log::instantInfo("{}Expanded pipeline: gst-launch-1.0 {}{}\n",
                              kAnsiPipeline,
                              expanded_pipeline,
                              kAnsiReset);
        return;
    }

    opk::log::instantInfo("gst-launch-1.0 {} \n", expanded_pipeline);
}

struct FileCheckSummary {
    size_t missingModelFiles{};
    size_t missingMediaFiles{};
    bool missingExecuTorchPlugin{};
    bool discoveredFiles{};

    [[nodiscard]] bool has_missing_files() const {
        return missingModelFiles > 0 || missingMediaFiles > 0;
    }

    [[nodiscard]] bool has_blocking_issues() const {
        return has_missing_files() || missingExecuTorchPlugin;
    }
};

static void print_file_status(std::string_view kind, std::string_view path, bool exists) {
    const auto status = exists ? std::string_view{"OK"} : std::string_view{"MISSING"};
    const auto line = std::format("{}: {} {}", kind, status, path);

    if (stdout_style_enabled()) {
        const auto style = exists ? kAnsiPresentFile : kAnsiMissingFile;
        opk::log::instantInfo("{}{}{}\n", style, line, kAnsiReset);
        return;
    }

    opk::log::instantInfo("{}\n", line);
}

static void print_remote_media_status(std::string_view path) {
    opk::log::instantInfo("Media: REMOTE {}\n", path);
}

static void print_missing_file_message(const FileCheckSummary &summary, bool interactive) {
    if (summary.missingModelFiles > 0 && summary.missingMediaFiles > 0) {
        opk::log::instantInfo("You have model files missing, and you have media files missing.\n");
    } else if (summary.missingModelFiles > 0) {
        opk::log::instantInfo("You have model files missing.\n");
    } else if (summary.missingMediaFiles > 0) {
        opk::log::instantInfo("You have media files missing.\n");
    }

    if (summary.has_missing_files())
        opk::log::instantInfo("Download the missing files before running this pipeline.\n");
    if (summary.missingModelFiles > 0)
        opk::log::instantInfo(
            "Use scripts/download-models.py --models-dir config/models to download models.\n");
    if (summary.missingMediaFiles > 0)
        opk::log::instantInfo("Use scripts/private/download-demo-videos.sh to download media.\n");
    if (summary.missingExecuTorchPlugin) {
        opk::log::instantInfo(
            "ExecuTorch model detected, but the ExecuTorch OPK ops plugin was not found.\n");
        opk::log::instantInfo("Build OPK with ExecuTorch support enabled before running this "
                              "pipeline. Check the documentation for details.\n");
    }
    if (interactive && stdout_style_enabled()) {
        opk::log::instantInfo("{}Press ESC to return to menu.{}\n", kAnsiReverse, kAnsiReset);
    } else if (interactive) {
        opk::log::instantInfo("Press ESC to return to menu.\n");
    }
}

static FileCheckSummary print_discovered_files(std::string_view expanded_pipeline) {
    FileCheckSummary summary;
    std::set<std::string, std::less<>> printed_model_files;
    std::vector<std::string> discovered_model_files;
    for (const auto &opchain_path : opk::menu::discover_opchain_paths(expanded_pipeline)) {
        auto model_files = opk::menu::discover_model_files(opchain_path);
        if (!model_files.has_value()) {
            opk::log::instantError(
                "Model discovery failed for {}: {}\n", opchain_path, model_files.error().info);
            ++summary.missingModelFiles;
            continue;
        }

        for (const auto &model_file : *model_files) {
            if (!printed_model_files.insert(model_file).second)
                continue;

            summary.discoveredFiles = true;
            discovered_model_files.push_back(model_file);
            const bool exists = file_exists(model_file);
            if (!exists)
                ++summary.missingModelFiles;
            print_file_status("Model", model_file, exists);
        }
    }

    const auto executorch_status =
        opk::menu::check_executorch_dependency(discovered_model_files, plugin_path().path);
    if (executorch_status.required) {
        summary.discoveredFiles = true;
        if (!executorch_status.pluginFound)
            summary.missingExecuTorchPlugin = true;
        print_file_status(
            "ExecuTorch plugin", executorch_status.pluginPath, executorch_status.pluginFound);
    }

    for (const auto &media_file : opk::menu::discover_media_files(expanded_pipeline)) {
        summary.discoveredFiles = true;
        if (opk::menu::is_remote_reference(media_file)) {
            print_remote_media_status(media_file);
            continue;
        }

        const bool exists = file_exists(media_file);
        if (!exists)
            ++summary.missingMediaFiles;
        print_file_status("Media", media_file, exists);
    }

    if (summary.discoveredFiles) {
        opk::log::instantInfo("Missing model files: {}\n", summary.missingModelFiles);
        opk::log::instantInfo("Missing media files: {}\n", summary.missingMediaFiles);
    }

    return summary;
}

static std::string format_seconds(std::int64_t total_seconds) {
    const auto hours = total_seconds / 3600;
    const auto minutes = (total_seconds / 60) % 60;
    const auto seconds = total_seconds % 60;
    if (hours > 0)
        return std::format("{}:{:02}:{:02}", hours, minutes, seconds);
    return std::format("{}:{:02}", minutes, seconds);
}

static std::string format_nanoseconds(std::int64_t nanoseconds) {
    return format_seconds(nanoseconds / 1'000'000'000);
}

static std::string format_progress_status(char spinner,
                                          const opk::runtime::Pipeline::Progress &progress,
                                          std::optional<size_t> detection_count,
                                          bool back_key_enabled) {
    const auto detections =
        detection_count.has_value() ? std::format("{}", *detection_count) : std::string{"--"};
    const auto prefix =
        back_key_enabled ? std::format("{} ESC - Back | ", spinner) : std::format("{} ", spinner);
    if (progress.hasPosition)
        return std::format("{}Time: {} | Detections: {} (see results on web page)",
                           prefix,
                           format_nanoseconds(progress.positionNs),
                           detections);
    return std::format(
        "{}Time: --:-- | Detections: {} (see results on web page)", prefix, detections);
}

template <typename Container> static size_t count_present_entries(const Container &entries) {
    return static_cast<size_t>(
        std::ranges::count_if(entries, [](const auto &entry) { return entry != nullptr; }));
}

static size_t count_display_detections(const open_perception_kit::FrameResults &frame_results) {
    size_t count = 0;
    frame_results.for_each<open_perception_kit::metadata::BoxDetectionsT>(
        [&count](const auto &payload) { count += count_present_entries(payload.detections); });
    frame_results.for_each<open_perception_kit::metadata::SegmentationMasksT>(
        [&count](const auto &payload) { count += count_present_entries(payload.masks); });
    return count;
}

static void render_progress_status(bool enabled, std::string_view line) {
    if (!enabled)
        return;

    if (terminal_style_enabled()) {
        std::fprintf(
            stderr, "\r\x1b[2K\x1b[7m%.*s\x1b[0m", static_cast<int>(line.size()), line.data());
    } else {
        std::fprintf(stderr, "\r\x1b[2K%.*s", static_cast<int>(line.size()), line.data());
    }
    std::fflush(stderr);
}

static void clear_progress_status(bool enabled) {
    if (!enabled)
        return;

    std::fputs("\r\x1b[2K", stderr);
    std::fflush(stderr);
}

class PipelineBackKeyInput {
  public:
    explicit PipelineBackKeyInput(bool enabled) {
        if (!enabled)
            return;
        if (tcgetattr(STDIN_FILENO, &original_attributes_) != 0)
            return;

        termios attributes = original_attributes_;
        attributes.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
        attributes.c_cc[VMIN] = 0;
        attributes.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &attributes) != 0)
            return;

        active_ = true;
    }

    PipelineBackKeyInput(const PipelineBackKeyInput &) = delete;
    PipelineBackKeyInput &operator=(const PipelineBackKeyInput &) = delete;

    ~PipelineBackKeyInput() {
        if (active_)
            (void)tcsetattr(STDIN_FILENO, TCSANOW, &original_attributes_);
    }

    [[nodiscard]] bool active() const {
        return active_;
    }

    [[nodiscard]] bool escape_pressed() const {
        if (!active_)
            return false;

        pollfd descriptor{STDIN_FILENO, POLLIN, 0};
        const int ready = poll(&descriptor, 1, 0);
        if (ready <= 0 || (descriptor.revents & POLLIN) == 0)
            return false;

        std::array<char, 16> input{};
        // poll() and VMIN=0/VTIME=0 make this terminal read non-blocking.
        const auto count = read(STDIN_FILENO, input.data(), input.size()); // NOSONAR
        if (count <= 0)
            return false;

        const auto end = input.begin() + count;
        return std::find(input.begin(), end, '\x1b') != end;
    }

  private:
    termios original_attributes_{};
    bool active_{};
};

static bool wait_for_escape_to_return() {
    PipelineBackKeyInput input(pipeline_back_key_enabled());
    if (!input.active())
        return false;

    while (requested_termination_signal == 0) {
        if (input.escape_pressed())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return false;
}

static void record_termination_signal(int signal_number) {
    requested_termination_signal = signal_number;
}

class TerminationSignalHandlers {
  public:
    bool install() {
        struct sigaction action{};
        action.sa_handler = record_termination_signal;
        sigemptyset(&action.sa_mask);

        if (sigaction(SIGINT, &action, &previous_sigint_) < 0)
            return false;
        sigint_installed_ = true;

        if (sigaction(SIGTERM, &action, &previous_sigterm_) < 0) {
            const int install_error = errno;
            (void)sigaction(SIGINT, &previous_sigint_, nullptr);
            sigint_installed_ = false;
            errno = install_error;
            return false;
        }
        sigterm_installed_ = true;
        return true;
    }

    ~TerminationSignalHandlers() {
        if (sigterm_installed_)
            (void)sigaction(SIGTERM, &previous_sigterm_, nullptr);
        if (sigint_installed_)
            (void)sigaction(SIGINT, &previous_sigint_, nullptr);
    }

    TerminationSignalHandlers(const TerminationSignalHandlers &) = delete;
    TerminationSignalHandlers &operator=(const TerminationSignalHandlers &) = delete;

    TerminationSignalHandlers() = default;

  private:
    struct sigaction previous_sigint_{};
    struct sigaction previous_sigterm_{};
    bool sigint_installed_ = false;
    bool sigterm_installed_ = false;
};

static fs::path last_selection_path() {
    return pipelines_directory() / kLastSelectionFileName;
}

static std::optional<std::string> load_last_selected_pipeline() {
    std::ifstream in(last_selection_path());
    if (!in.is_open())
        return std::nullopt;

    std::string id;
    std::getline(in, id);
    id = opk::utf8::trim(id);
    if (id.empty())
        return std::nullopt;
    return fs::path(id).filename().string();
}

static bool save_last_selected_pipeline(const std::string &pipeline) {
    std::ofstream out(last_selection_path(), std::ios::trunc);
    if (!out.is_open())
        return false;
    out << pipeline << "\n";
    return true;
}

static void clear_last_selected_pipeline() {
    std::error_code ec;
    fs::remove(last_selection_path(), ec);
}

static std::optional<PipelineEntry> load_entry_from_json_file(const fs::path &p) {
    auto preset = opk::PipelinePreset::fromFile(p.string());
    if (!preset) {
        opk::log::instantError("{}\n", preset.error().info);
        return std::nullopt;
    }

    return PipelineEntry{
        .id = p.filename().string(),
        .description = std::move(*preset->description),
        .sourceInfo = preset->sourceInfo.value_or(""),
        .pipeline = std::move(preset->pipeline),
        .loop = preset->loop,
    };
}

static std::vector<PipelineEntry> enumerate_entries() {
    std::vector<PipelineEntry> entries;
    const fs::path directory = pipelines_directory();

    std::error_code ec;
    if (!fs::exists(directory, ec) || !fs::is_directory(directory, ec)) {
        opk::log::instantError("Directory not found or not a directory: {}\n", directory.string());
        return entries;
    }

    for (const auto &fileSystemObject : fs::directory_iterator(directory, ec)) {
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

    std::ranges::sort(entries, [](const auto &a, const auto &b) { return a.id < b.id; });

    return entries;
}

// Resolves a pipeline argument which can be either:
// - A full path to a JSON file
// - A pipeline ID/stem (resolved against the configured pipeline directory with/without .json
// extension)
static std::optional<std::string> resolve_pipeline_path(const fs::path &requested) {

    // First, check if it's already a valid full path
    if (file_exists(requested)) {
        return requested;
    }

    // Try to resolve as an ID in the configured pipeline directory:
    // 1. <pipelines_directory>/<requested>.json
    // 2. <pipelines_directory>/<requested>
    const fs::path directory = pipelines_directory();

    auto requested_with_extension = requested;
    requested_with_extension.replace_extension(".json");
    if (fs::path candidate1 = directory / requested_with_extension; file_exists(candidate1)) {
        return candidate1.string();
    }

    if (fs::path candidate2 = directory / requested; file_exists(candidate2)) {
        return candidate2.string();
    }

    return std::nullopt;
}

int run_pipeline(const std::string &pipeline_description,
                 bool dry_run,
                 bool loop,
                 const opk::runtime::Pipeline::StartOptions &start_options,
                 bool allow_back_to_menu) {
    auto expanded = opk::expandPipelineDescription(pipeline_description);
    if (!expanded) {
        opk::log::instantError("error: {}\n", expanded.error().info);
        return 3;
    }

    requested_termination_signal = 0;
    TerminationSignalHandlers signal_handlers;
    if (!dry_run && !signal_handlers.install()) {
        opk::log::instantError("sigaction: {}\n", std::strerror(errno));
        return kExecutionFailureExitCode;
    }

    // Keep the familiar gst-launch form as a copyable diagnostic even though
    // execution now happens through the in-process Runtime.
    print_expanded_pipeline(*expanded);
    const auto file_check = print_discovered_files(*expanded);
    std::fflush(stdout);
    if (file_check.has_blocking_issues()) {
        const bool interactive = allow_back_to_menu && !dry_run && pipeline_back_key_enabled();
        print_missing_file_message(file_check, interactive);
        if (dry_run)
            return 0;
        if (interactive && wait_for_escape_to_return())
            return kBackToMenuCode;
        if (requested_termination_signal != 0)
            return kSignalExitCodeOffset + requested_termination_signal;
        return 1;
    }
    if (dry_run)
        return 0;

    const auto plugins = plugin_path();
    std::error_code ec;
    if (plugins.required || fs::is_directory(plugins.path, ec)) {
        auto plugin_path_result = opk::runtime::Pipeline::addPluginPath(plugins.path);
        if (requested_termination_signal != 0)
            return kSignalExitCodeOffset + requested_termination_signal;
        if (!plugin_path_result) {
            opk::log::instantError("GStreamer plugin setup failed: {}\n",
                                   plugin_path_result.error().info);
            return 1;
        }
    }

    auto pipeline_result = opk::runtime::Pipeline::fromString(pipeline_description);
    if (requested_termination_signal != 0)
        return kSignalExitCodeOffset + requested_termination_signal;
    if (!pipeline_result) {
        opk::log::instantError("Pipeline setup failed: {}\n", pipeline_result.error().info);
        return 1;
    }

    std::mutex completion_mutex;
    std::condition_variable completion_cv;
    bool completed = false;
    auto mark_completed = [&]() {
        {
            std::lock_guard lock(completion_mutex);
            completed = true;
        }
        completion_cv.notify_all();
    };

    pipeline_result->onEos(mark_completed);
    pipeline_result->onError([&](const opk::runtime::Error &) { mark_completed(); });

    const bool show_progress = progress_status_enabled();
    PipelineBackKeyInput back_key_input(show_progress && pipeline_back_key_enabled());
    std::mutex progress_mutex;
    opk::runtime::Pipeline::Progress latest_progress;
    std::optional<size_t> latest_detection_count;
    if (show_progress) {
        pipeline_result->onProgress([&](const opk::runtime::Pipeline::Progress &progress) {
            std::lock_guard lock(progress_mutex);
            latest_progress = progress;
        });
        pipeline_result->onFrameResultsPacket([&](const std::vector<std::uint8_t> &packet) {
            try {
                open_perception_kit::FrameResults frame_results(
                    std::span<const std::uint8_t>{packet.data(), packet.size()});
                if (!frame_results.valid()) {
                    return;
                }

                const auto detection_count = count_display_detections(frame_results);
                std::lock_guard lock(progress_mutex);
                latest_detection_count = detection_count;
            } catch (const std::exception &error) { // NOSONAR
                // Progress decode must not abort playback.
                opk::log::debug("Failed to decode FrameResults progress packet: {}\n",
                                error.what());
                std::lock_guard lock(progress_mutex);
                latest_detection_count.reset();
            }
        });
    }

    auto options = start_options;
    options.loop = loop;
    auto start_result = pipeline_result->start(options);
    if (!start_result) {
        opk::log::instantError("Pipeline start failed: {}\n", start_result.error().info);
        return 1;
    }

    auto next_progress_render = std::chrono::steady_clock::now();
    size_t spinner_index = 0;
    constexpr std::array<char, 4> progress_spinner{'|', '/', '-', '\\'};
    bool back_requested = false;

    {
        std::unique_lock lock(completion_mutex);
        while (!completed && requested_termination_signal == 0) {
            completion_cv.wait_for(lock, std::chrono::milliseconds(100));
            if (back_key_input.escape_pressed()) {
                back_requested = true;
                break;
            }

            const auto now = std::chrono::steady_clock::now();
            if (show_progress && now >= next_progress_render) {
                opk::runtime::Pipeline::Progress progress;
                std::optional<size_t> detection_count;
                {
                    std::lock_guard progress_lock(progress_mutex);
                    progress = latest_progress;
                    detection_count = latest_detection_count;
                }
                render_progress_status(show_progress,
                                       format_progress_status(progress_spinner[spinner_index],
                                                              progress,
                                                              detection_count,
                                                              back_key_input.active()));
                spinner_index = (spinner_index + 1) % progress_spinner.size();
                next_progress_render = now + kProgressRenderInterval;
            }
        }
    }

    clear_progress_status(show_progress);

    if (back_requested) {
        auto stop_result = pipeline_result->stop();
        if (!stop_result)
            opk::log::instantError("Pipeline stop failed: {}\n", stop_result.error().info);
        return kBackToMenuCode;
    }

    if (requested_termination_signal != 0) {
        const int signal_number = requested_termination_signal;
        auto stop_result = pipeline_result->stop();
        if (!stop_result)
            opk::log::instantError("Pipeline stop failed: {}\n", stop_result.error().info);
        return kSignalExitCodeOffset + signal_number;
    }

    auto wait_result = pipeline_result->wait();
    if (!wait_result) {
        opk::log::instantError("Pipeline execution failed: {}\n", wait_result.error().info);
        return 1;
    }

    return 0;
}

static std::optional<opk::runtime::LogLevel> parse_log_level(std::string_view value) {
    if (value == "0" || value == "off")
        return opk::runtime::LogLevel::Off;
    if (value == "1" || value == "error")
        return opk::runtime::LogLevel::Error;
    if (value == "2" || value == "warn")
        return opk::runtime::LogLevel::Warn;
    if (value == "3" || value == "notice")
        return opk::runtime::LogLevel::Notice;
    if (value == "4" || value == "info")
        return opk::runtime::LogLevel::Info;
    if (value == "5" || value == "debug")
        return opk::runtime::LogLevel::Debug;
    return std::nullopt;
}

static bool parse_log_targets(std::string_view value,
                              opk::runtime::Pipeline::StartOptions &options) {
    bool stdout_enabled = false;
    bool stderr_enabled = false;
    bool file_enabled = false;

    if (value == "none") {
        options.logToStdout = false;
        options.logToStderr = false;
        options.logToFile = false;
        return true;
    }
    if (value.empty())
        return false;

    size_t token_start = 0;
    while (token_start < value.size()) {
        const size_t token_end = value.find(',', token_start);
        const auto token = value.substr(token_start, token_end - token_start);
        if (token == "stdout") {
            stdout_enabled = true;
        } else if (token == "stderr") {
            stderr_enabled = true;
        } else if (token == "file") {
            file_enabled = true;
        } else {
            return false;
        }

        if (token_end == std::string_view::npos)
            break;
        token_start = token_end + 1;
    }

    if (value.back() == ',')
        return false;

    options.logToStdout = stdout_enabled;
    options.logToStderr = stderr_enabled;
    options.logToFile = file_enabled;
    return true;
}

// clang-format off
static void print_usage(const char *argv0) {
    opk::log::instantInfo(
        "Usage:\n"
        "  {} [options] [pipeline]\n"
        "\n"
        "Selection:\n"
        "  -l, --last                 run the last selected pipeline\n"
        "  -p, --dry-run              only print the pipeline without executing it\n"
        "  -h, --help                 print this help\n"
        "  pipeline                   pipeline ID (e.g. 'onnx') or JSON path; cannot be combined with --last\n"
        "\n"
        "Runtime logging:\n"
        "  --log-level LEVEL          0..5 or off/error/warn/notice/info/debug (default: error)\n"
        "  --log-targets TARGETS      comma-separated stdout,stderr,file, or none (default: stderr)\n"
        "\n"
        "Environment:\n"
        "  OPK_PROJECT_ROOT=/work               # project checkout root (default: /work)\n"
        "  NO_COLOR=1                          # disable colors in the interactive menu\n"
        "  OPK_LOG_FILE=opk.log               # file target path (default: opk.log; enable with --log-targets)\n",
        argv0);
}
// clang-format on

static int parse_args(int argc,
                      char **argv,
                      bool &run_last,
                      bool &dry_run,
                      std::optional<std::string> &requested_pipeline,
                      opk::runtime::Pipeline::StartOptions &start_options) {
    requested_pipeline.reset();

    opterr = 0; // we'll print our own usage

    enum class LongOption {
        LogLevel = 256,
        LogTargets,
    };
    const std::array<option, 6> long_options{{
        {"last", no_argument, nullptr, 'l'},
        {"dry-run", no_argument, nullptr, 'p'},
        {"help", no_argument, nullptr, 'h'},
        {"log-level", required_argument, nullptr, static_cast<int>(LongOption::LogLevel)},
        {"log-targets", required_argument, nullptr, static_cast<int>(LongOption::LogTargets)},
        {nullptr, 0, nullptr, 0},
    }};

    int opt;
    while ((opt = getopt_long(argc, argv, ":lph", long_options.data(), nullptr)) != -1) {
        switch (opt) {
        case 'l':
            run_last = true;
            break;
        case 'p':
            dry_run = true;
            break;
        case static_cast<int>(LongOption::LogLevel): {
            const auto log_level = parse_log_level(optarg);
            if (!log_level) {
                opk::log::instantError("Invalid log level '{}'.\n", optarg);
                print_usage(argv[0]);
                return 2;
            }
            start_options.logLevel = *log_level;
            break;
        }
        case static_cast<int>(LongOption::LogTargets):
            if (!parse_log_targets(optarg, start_options)) {
                opk::log::instantError("Invalid log targets '{}'.\n", optarg);
                print_usage(argv[0]);
                return 2;
            }
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

static PipelineIndex index_pipeline_entries(const std::vector<PipelineEntry> &entries) {
    PipelineIndex id_to_idx;
    id_to_idx.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        id_to_idx[entries[i].id] = i;
    }
    return id_to_idx;
}

static std::optional<size_t> find_last_pipeline_index(const PipelineIndex &id_to_idx);

static int run_last_pipeline(const std::vector<PipelineEntry> &entries,
                             const PipelineIndex &id_to_idx,
                             bool dry_run,
                             const opk::runtime::Pipeline::StartOptions &start_options) {
    const auto last_pipeline_idx = find_last_pipeline_index(id_to_idx);
    if (!last_pipeline_idx.has_value()) {
        opk::log::instantInfo("No valid previous selection stored ({}).\n",
                              last_selection_path().string());
        return 3;
    }

    const auto &pipeline_entry = entries[*last_pipeline_idx];
    (void)save_last_selected_pipeline(pipeline_entry.id);
    return run_pipeline(
        pipeline_entry.pipeline, dry_run, pipeline_entry.loop, start_options, false);
}

static int run_requested_pipeline(const std::string &requested_pipeline,
                                  const fs::path &configured_pipelines_directory,
                                  bool dry_run,
                                  const opk::runtime::Pipeline::StartOptions &start_options) {
    const auto resolved = resolve_pipeline_path(requested_pipeline);
    if (!resolved) {
        opk::log::instantInfo("Pipeline not found: '{}' (expected full path or ID in {})\n",
                              requested_pipeline,
                              configured_pipelines_directory.string());
        return 3;
    }

    const auto entry = load_entry_from_json_file(*resolved);
    if (!entry) {
        opk::log::instantInfo("Failed to load pipeline from: {}\n", *resolved);
        return 3;
    }

    // Since this path might not be available in the menu, do not save it as the last selected
    // pipeline.
    return run_pipeline(entry->pipeline, dry_run, entry->loop, start_options, false);
}

static std::optional<size_t> find_last_pipeline_index(const PipelineIndex &id_to_idx) {
    const auto last_pipeline = load_last_selected_pipeline();
    if (!last_pipeline)
        return std::nullopt;

    const auto it = id_to_idx.find(*last_pipeline);
    if (it == id_to_idx.end()) {
        clear_last_selected_pipeline();
        return std::nullopt;
    }

    return it->second;
}

static int run_pipeline_menu(const fs::path &configured_pipelines_directory,
                             const std::vector<PipelineEntry> &entries,
                             const PipelineIndex &id_to_idx,
                             bool dry_run,
                             const opk::runtime::Pipeline::StartOptions &start_options) {
    std::vector<opk::menu::Entry> menu_entries;
    menu_entries.reserve(entries.size());
    for (const auto &entry : entries)
        menu_entries.emplace_back(entry.id, entry.description, entry.sourceInfo);

    while (true) {
        const auto last_pipeline_idx = find_last_pipeline_index(id_to_idx);
        const auto selection = opk::menu::select_pipeline(
            configured_pipelines_directory.string(), menu_entries, last_pipeline_idx);
        if (selection.kind == opk::menu::ResultKind::Interrupted)
            return kSignalExitCodeOffset + selection.signal_number;
        if (selection.kind == opk::menu::ResultKind::Cancelled)
            return 0;

        const auto &pipeline_entry = entries.at(selection.selected_index);
        if (!save_last_selected_pipeline(pipeline_entry.id)) {
            opk::log::instantInfo("Warning: failed to save last selected pipeline to {}\n",
                                  last_selection_path().string());
        }

        const int result = run_pipeline(
            pipeline_entry.pipeline, dry_run, pipeline_entry.loop, start_options, true);
        if (result != kBackToMenuCode)
            return result;
    }
}

static int normalize_exit_code(int code) {
    return code == kBackToMenuCode ? 0 : code;
}

int main(int argc, char **argv) {
    // Parse XOR args: "-l" OR "<pipeline>" OR none
    bool run_last = false;
    bool dry_run = false;
    std::optional<std::string> requested_pipeline;
    opk::runtime::Pipeline::StartOptions start_options;

    auto rc = parse_args(argc, argv, run_last, dry_run, requested_pipeline, start_options);
    if (rc != 0)
        return rc;

    const fs::path configured_pipelines_directory = pipelines_directory();
    auto entries = enumerate_entries();
    if (entries.empty()) {
        opk::log::instantInfo("No valid pipelines found in: {}\n",
                              configured_pipelines_directory.string());
        return 1;
    }

    const auto id_to_idx = index_pipeline_entries(entries);

    if (run_last)
        return normalize_exit_code(run_last_pipeline(entries, id_to_idx, dry_run, start_options));

    if (requested_pipeline)
        return normalize_exit_code(run_requested_pipeline(
            *requested_pipeline, configured_pipelines_directory, dry_run, start_options));

    return normalize_exit_code(run_pipeline_menu(
        configured_pipelines_directory, entries, id_to_idx, dry_run, start_options));
}
