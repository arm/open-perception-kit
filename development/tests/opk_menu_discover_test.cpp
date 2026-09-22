/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include "discover.hpp"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace {

void write_model_descriptor(const std::filesystem::path &path, const std::string &model_file) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << nlohmann::json{
        {"version", "1.0.0"},
        {"name", "test"},
        {"modelFile", model_file},
        {"dynamicOutput", true},
        {"inputTensors",
         {{{"shape", {1}}, {"dataKind", "RawTensorData"}, {"valueType", "Float32"}}}},
    };
}

std::string normalized_absolute_path(const std::filesystem::path &path) {
    return std::filesystem::absolute(path).lexically_normal().string();
}

} // namespace

TEST(OpkMenuDiscover, FindsUniqueSupportedModelFilesFromOpChain) {
    const auto root = std::filesystem::path("opk_menu_discover_test");
    const auto opchain_directory = root / "opchains";
    const auto models_directory = root / "models";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(opchain_directory);

    write_model_descriptor(models_directory / "onnx" / "model.json", "model.onnx");
    write_model_descriptor(models_directory / "pte" / "model.json", "model.pte");
    write_model_descriptor(models_directory / "ignored" / "model.json", "labels.txt");

    const auto opchain_path = opchain_directory / "opchain.json";
    std::ofstream(opchain_path) << nlohmann::json{
        {"version", "1.0.0"},
        {"name", "discover"},
        {"description", "Discover model files."},
        {"ops",
         {{{"id", "opk-future-ops/Inference"},
           {"attributes", {{"modelDescriptor", "../models/onnx/model.json"}}}},
          {{"id", "opk-future-ops/Inference"},
           {"attributes", {{"modelDescriptor", "../models/pte/model.json"}}}},
          {{"id", "opk-future-ops/Inference"},
           {"attributes", {{"modelDescriptor", "../models/ignored/model.json"}}}},
          {{"id", "opk-future-ops/Inference"},
           {"attributes", {{"modelDescriptor", "../models/onnx/model.json"}}}}}},
    };

    const auto model_files = opk::menu::discover_model_files(opchain_path.string());

    ASSERT_TRUE(model_files.has_value()) << model_files.error().toString();
    ASSERT_EQ(model_files->size(), 2U);
    EXPECT_EQ((*model_files)[0], normalized_absolute_path(models_directory / "onnx/model.onnx"));
    EXPECT_EQ((*model_files)[1], normalized_absolute_path(models_directory / "pte/model.pte"));

    std::filesystem::remove_all(root);
}

TEST(OpkMenuDiscover, FindsOpChainsAndMediaFilesFromPipelineString) {
    const std::string pipeline =
        R"(filesrc location="/work/data/videos/sample movie.MOV" ! decodebin ! )"
        R"(opkinfer opchain-path='/work/config/models/yolov11/opchain.json' ! )"
        R"(opkosd bg-image=file:///work/data/images/background.JPG ! )"
        R"(filesrc location=https://example.com/remote.mp4?token=abc ! )"
        R"(multifilesrc location=/work/data/images/frame.png ! )"
        R"(filesink location=/work/data/output/new-output.mp4 ! )"
        R"(filesrc location="/work/data/videos/sample movie.MOV")";

    const auto opchains = opk::menu::discover_opchain_paths(pipeline);
    const auto media_files = opk::menu::discover_media_files(pipeline);

    ASSERT_EQ(opchains.size(), 1U);
    EXPECT_EQ(opchains[0], "/work/config/models/yolov11/opchain.json");

    ASSERT_EQ(media_files.size(), 4U);
    EXPECT_EQ(media_files[0], "/work/data/videos/sample movie.MOV");
    EXPECT_EQ(media_files[1], "/work/data/images/background.JPG");
    EXPECT_EQ(media_files[2], "https://example.com/remote.mp4?token=abc");
    EXPECT_EQ(media_files[3], "/work/data/images/frame.png");
}

TEST(OpkMenuDiscover, ChecksExecuTorchPluginWhenPteModelIsReferenced) {
    const auto root = std::filesystem::path("opk_menu_discover_executorch_test");
    const auto plugin_directory = root / "plugins";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(plugin_directory);

    const std::vector<std::string> onnx_models{"/work/model.onnx"};
    const auto not_required =
        opk::menu::check_executorch_dependency(onnx_models, plugin_directory.string());
    EXPECT_FALSE(not_required.required);
    EXPECT_FALSE(not_required.pluginFound);
    EXPECT_TRUE(not_required.pluginPath.empty());

    const std::vector<std::string> executorch_models{"/work/model.pte"};
    const auto missing =
        opk::menu::check_executorch_dependency(executorch_models, plugin_directory.string());
    ASSERT_TRUE(missing.required);
    EXPECT_FALSE(missing.pluginFound);
    EXPECT_EQ(missing.pluginPath,
              normalized_absolute_path(plugin_directory / "opk-executorch-ops.so"));

    std::ofstream(plugin_directory / "opk-executorch-ops.so") << "plugin";
    const auto present =
        opk::menu::check_executorch_dependency(executorch_models, plugin_directory.string());
    ASSERT_TRUE(present.required);
    EXPECT_TRUE(present.pluginFound);
    EXPECT_EQ(present.pluginPath, missing.pluginPath);

    std::filesystem::remove_all(root);
}
