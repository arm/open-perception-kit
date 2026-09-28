/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

#pragma once

#include "Validator.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace opk::config::test {

inline bool hasRule(const ValidationReport &report, std::string_view rule) {
    return std::ranges::any_of(report.issues,
                               [&](const auto &issue) { return issue.rule == rule; });
}

class TemporaryRepository {
  public:
    TemporaryRepository() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::current_path() / std::format("opk-config-validator-{}", suffix);
        if (!std::filesystem::create_directory(root_))
            throw std::filesystem::filesystem_error("temporary repository path already exists",
                                                    root_,
                                                    std::make_error_code(std::errc::file_exists));

        std::filesystem::create_directories(root_ / "config/schemas/v1");
        std::filesystem::create_directories(root_ / "config/models/first");
        std::filesystem::create_directories(root_ / "config/models/second");
        std::filesystem::create_directories(root_ / "config/opchains");
        std::filesystem::create_directories(root_ / "config/pipelines");

        const std::filesystem::path sourceRoot = OPK_REPOSITORY_ROOT;
        for (const auto *name :
             {"model.schema.json", "opchain.schema.json", "pipeline.schema.json"}) {
            std::filesystem::copy_file(sourceRoot / "config/schemas/v1" / name,
                                       root_ / "config/schemas/v1" / name);
        }
        std::filesystem::copy(sourceRoot / "config/schemas/v1/opchain",
                              root_ / "config/schemas/v1/opchain",
                              std::filesystem::copy_options::recursive);
    }

    ~TemporaryRepository() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    TemporaryRepository(const TemporaryRepository &) = delete;
    TemporaryRepository &operator=(const TemporaryRepository &) = delete;

    [[nodiscard]] const std::filesystem::path &root() const {
        return root_;
    }

    void writeModel(std::string_view directory, std::string_view name) const {
        nlohmann::json model{
            {"version", "1.0.0"},
            {"name", name},
            {"modelFile", "model.onnx"},
            {"dynamicOutput", true},
            {"inputTensors",
             {{{"shape", {1}}, {"dataKind", "RawTensorData"}, {"valueType", "Float32"}}}}};
        std::ofstream output(root_ / "config/models" / directory / "model.json");
        output << model;
    }

    void writeOpChain(std::string_view name) const {
        nlohmann::json opchain{
            {"version", "1.0.0"},
            {"name", name},
            {"description", "Repository name test."},
            {"ops", {{{"id", "custom/Operation"}, {"attributes", nlohmann::json::object()}}}}};
        std::ofstream output(root_ / "config/opchains/opchain.json");
        output << opchain;
    }

  private:
    std::filesystem::path root_;
};

} // namespace opk::config::test
