/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "pek/ModelDescriptor.h"

TEST(ModelDescriptor, ResolvesModelFileRelativeToDescriptor) {
    const auto directory =
        std::filesystem::temp_directory_path() / "pek_model_descriptor_test" / "nested";
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
