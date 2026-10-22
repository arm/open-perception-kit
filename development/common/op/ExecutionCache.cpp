/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "op/ExecutionCache.h"

#include "Log.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <glib.h>
#include <nlohmann/json.hpp>

namespace opk::op {
namespace {
constexpr size_t maximumCacheBytes{16 * 1024};
constexpr int cacheVersion{1};
constexpr int privateCachePermissions{0600};

std::filesystem::path cachePath(const std::string &setup) {
    const std::unique_ptr<gchar, decltype(&g_free)> digest{
        g_compute_checksum_for_string(G_CHECKSUM_SHA256, setup.c_str(), -1), g_free};
    if (!digest)
        return {};
    return std::filesystem::path{g_get_user_cache_dir()} / "open-perception-kit" / "preprocessing" /
           (std::string{digest.get()} + ".json");
}

std::optional<ExecutionCalibration::Combination> loadExecutionCache(const std::string &setup,
                                                                    const size_t stages) {
    const auto path{cachePath(setup)};
    try {
        std::ifstream stream{path};
        if (!stream)
            return std::nullopt;
        std::string contents(maximumCacheBytes + 1, '\0');
        stream.read(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (stream.gcount() > static_cast<std::streamsize>(maximumCacheBytes))
            return std::nullopt;
        contents.resize(static_cast<size_t>(stream.gcount()));
        const auto document = nlohmann::json::parse(contents);
        if (document.at("version") != cacheVersion || !document.at("selection").is_array() ||
            document.at("selection").size() != stages)
            return std::nullopt;
        ExecutionCalibration::Combination selection;
        for (const auto &value : document.at("selection")) {
            if (!value.is_number_integer())
                return std::nullopt;
            if (value < static_cast<int>(PreprocessPath::Cpu) ||
                value > static_cast<int>(PreprocessPath::ReadbackGpu))
                return std::nullopt;
            selection.push_back(static_cast<PreprocessPath>(value.get<int>()));
        }
        return selection;
    } catch (const std::exception &) {
        log::debug("Ignoring invalid preprocessing cache.\n");
        return std::nullopt;
    }
}

void saveExecutionCache(const std::string &setup,
                        const ExecutionCalibration::Combination &selection) {
    const auto path{cachePath(setup)};
    if (path.empty())
        return;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        log::warning("Cannot save preprocessing choice: {}\n", error.message());
        return;
    }
    auto values = nlohmann::json::array();
    for (const auto choice : selection)
        values.push_back(static_cast<int>(choice));
    const auto contents{nlohmann::json{{"version", cacheVersion}, {"selection", values}}.dump()};
    GError *writeError{nullptr};
    if (!g_file_set_contents_full(path.c_str(),
                                  contents.c_str(),
                                  static_cast<gssize>(contents.size()),
                                  G_FILE_SET_CONTENTS_CONSISTENT,
                                  privateCachePermissions,
                                  &writeError)) {
        log::warning("Cannot save preprocessing choice: {}\n", writeError->message);
        g_error_free(writeError);
    }
}

void discardExecutionCache(const std::string &setup) {
    std::error_code ignored;
    std::filesystem::remove(cachePath(setup), ignored);
}

} // namespace

ExecutionCache::~ExecutionCache() {
    if (writer.valid()) {
        try {
            writer.get();
        } catch (const std::exception &error) {
            log::debug("Preprocessing cache write failed: {}\n", error.what());
        }
    }
    for (const auto &task : pending) {
        try {
            task();
        } catch (const std::exception &error) {
            log::debug("Preprocessing cache write failed: {}\n", error.what());
        }
    }
}

std::optional<ExecutionCalibration::Combination> ExecutionCache::load(const std::string &setup,
                                                                      const size_t stages) const {
    return invalidated.contains(setup) ? std::nullopt : loadExecutionCache(setup, stages);
}

void ExecutionCache::save(const std::string &setup,
                          const ExecutionCalibration::Combination &selection) {
    pending.push_back([setup, selection] { saveExecutionCache(setup, selection); });
    poll();
}

void ExecutionCache::discard(const std::string &setup) {
    if (!setup.empty() && invalidated.insert(setup).second) {
        pending.push_back([setup] { discardExecutionCache(setup); });
        poll();
    }
}

void ExecutionCache::poll() {
    if (writer.valid()) {
        if (writer.wait_for(std::chrono::seconds::zero()) != std::future_status::ready)
            return;
        try {
            writer.get();
        } catch (const std::exception &error) {
            log::debug("Preprocessing cache write failed: {}\n", error.what());
        }
    }
    if (pending.empty())
        return;
    try {
        // Copy before launch so a failed thread creation leaves the queued operations intact.
        writer = std::async(std::launch::async, [tasks = pending] {
            for (const auto &task : tasks)
                task();
        });
        pending.clear();
    } catch (const std::exception &error) {
        log::debug("Cannot start preprocessing cache writer: {}\n", error.what());
    }
}

} // namespace opk::op
