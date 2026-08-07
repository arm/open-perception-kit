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
    const auto descriptorPath = directory / "model.json";
    std::ofstream(descriptorPath)
        << R"({"name":"test","modelFile":"model.onnx","modelFamily":"test","dynamicOutput":false})";

    auto descriptor = pek::ModelDescriptor::fromFile(descriptorPath.string());

    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->modelFile, (directory / "model.onnx").string());
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
                 std::runtime_error);
}
