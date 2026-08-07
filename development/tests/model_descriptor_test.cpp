/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "pek/ModelDescriptor.h"

TEST(ModelDescriptor, ResolvesModelFileRelativeToDescriptor) {
    const auto directory = std::filesystem::path("pek_model_descriptor_test") / "nested";
    std::filesystem::remove_all(directory.parent_path());
    std::filesystem::create_directories(directory);
    const auto descriptorPath = directory / "model-variant.json";
    std::ofstream(descriptorPath) << R"({
        "version": 1,
        "name": "test",
        "modelFile": "model.onnx",
        "dynamicOutput": true,
        "inputTensors": [
            {"shape": [1], "dataKind": "RawTensorData", "valueType": "Float32"}
        ]
    })";

    auto descriptor = pek::ModelDescriptor::fromFile(descriptorPath.string());

    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->modelFile, (directory / "model.onnx").string());
    std::filesystem::remove_all(directory.parent_path());
}

TEST(ModelDescriptor, PreservesAbsoluteModelFileFilesystemMeaning) {
    const auto directory = std::filesystem::path("pek_model_descriptor_test") / "absolute";
    std::filesystem::remove_all(directory.parent_path());
    std::filesystem::create_directories(directory);
    const auto targetDirectory = directory.parent_path() / "absolute-target";
    std::filesystem::create_directories(targetDirectory / "child");
    std::filesystem::create_directory_symlink(std::filesystem::absolute(targetDirectory / "child"),
                                              directory / "linked");
    std::ofstream(targetDirectory / "model.onnx") << "target";
    std::ofstream(directory / "model.onnx") << "lexically-normalized";
    const auto descriptorPath = directory / "model.json";
    const auto modelPath = std::filesystem::absolute(directory) / "linked/../model.onnx";
    std::ofstream(descriptorPath) << nlohmann::json{
        {"version", 1},
        {"name", "test"},
        {"modelFile", modelPath.string()},
        {"dynamicOutput", true},
        {"inputTensors",
         {{{"shape", {1}}, {"dataKind", "RawTensorData"}, {"valueType", "Float32"}}}}};

    const auto descriptor = pek::ModelDescriptor::fromFile(descriptorPath.string());

    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->modelFile, modelPath.string());
    std::string marker;
    std::ifstream(descriptor->modelFile) >> marker;
    EXPECT_EQ(marker, "target");
    std::filesystem::remove_all(directory.parent_path());
}

TEST(ModelDescriptor, RejectsUriModelFile) {
    const auto descriptor = pek::ModelDescriptor::fromJson(R"({
        "version": 1,
        "name": "test",
        "modelFile": "hf:Arm/example@revision#file=model.onnx",
        "dynamicOutput": true,
        "inputTensors": [
            {"shape": [1], "dataKind": "RawTensorData", "valueType": "Float32"}
        ]
    })");

    ASSERT_FALSE(descriptor.has_value());
    EXPECT_NE(descriptor.error().info.find("schema.validation"), std::string::npos);
}

TEST(ModelDescriptor, RejectsOpChainFilename) {
    const auto directory = std::filesystem::path("pek_model_descriptor_test") / "wrong-name";
    std::filesystem::remove_all(directory.parent_path());
    std::filesystem::create_directories(directory);
    const auto descriptorPath = directory / "opchain.json";
    std::ofstream(descriptorPath) << R"({
        "version": 1,
        "name": "test",
        "modelFile": "model.onnx",
        "dynamicOutput": true,
        "inputTensors": [
            {"shape": [1], "dataKind": "RawTensorData", "valueType": "Float32"}
        ]
    })";

    const auto descriptor = pek::ModelDescriptor::fromFile(descriptorPath.string());

    ASSERT_FALSE(descriptor.has_value());
    EXPECT_NE(descriptor.error().info.find("dispatch.filename"), std::string::npos);
    std::filesystem::remove_all(directory.parent_path());
}

TEST(ModelDescriptor, ParsesTensorFeedbackMode) {
    const auto feedback =
        nlohmann::json{
            {"mode", "Copy"},
            {"fromOutputTensorIndex", 1},
            {"toInputTensorIndex", 2},
        }
            .get<pek::TensorFeedback>();

    EXPECT_EQ(feedback.mode, pek::TensorFeedback::Mode::Copy);
    EXPECT_THROW((nlohmann::json{
                     {"mode", "Unknown"},
                     {"fromOutputTensorIndex", 1},
                     {"toInputTensorIndex", 2},
                 }
                      .get<pek::TensorFeedback>()),
                 nlohmann::json::type_error);
}
