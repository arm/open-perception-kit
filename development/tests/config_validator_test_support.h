/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "Validator.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace pek::config::test {

inline bool hasRule(const ValidationReport &report, std::string_view rule) {
    return std::ranges::any_of(report.issues,
                               [&](const auto &issue) { return issue.rule == rule; });
}

class TemporaryRepository {
  public:
    TemporaryRepository() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ =
            std::filesystem::current_path() / ("pek-config-validator-" + std::to_string(suffix));
        if (!std::filesystem::create_directory(root_))
            throw std::runtime_error("temporary repository path already exists");

        std::filesystem::create_directories(root_ / "config/schemas/v1");
        std::filesystem::create_directories(root_ / "config/models/first");
        std::filesystem::create_directories(root_ / "config/models/second");
        std::filesystem::create_directories(root_ / "config/opchains");

        const std::filesystem::path sourceRoot = PEK_REPOSITORY_ROOT;
        for (const auto *name : {"model.schema.json", "opchain.schema.json"}) {
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
            {"version", 1},
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
            {"version", 1},
            {"name", name},
            {"description", "Repository name test."},
            {"ops", {{{"id", "custom/Operation"}, {"attributes", nlohmann::json::object()}}}}};
        std::ofstream output(root_ / "config/opchains/opchain.json");
        output << opchain;
    }

  private:
    std::filesystem::path root_;
};

} // namespace pek::config::test
