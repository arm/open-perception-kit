/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "op/OpChainDescriptor.h"

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

TEST(OpChainDescriptorTest, ResolvesRelativeModelDescriptorFromOpchainDirectory) {
    const auto root = std::filesystem::current_path() / "pek-opchain-descriptor-test";
    std::filesystem::remove_all(root);

    std::filesystem::create_directories(root / "nested");
    const auto opchainPath = root / "nested/opchain.json";
    {
        std::ofstream stream(opchainPath);
        stream << R"({
            "ops": [{
                "id": "pek-onnx-ops/Inference",
                "attributes": {"modelDescriptor": "../models/model.json"}
            }]
        })";
    }

    const auto descriptor = pek::op::OpChainDescriptor::fromFile(opchainPath.string());

    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->ops[0].attributes.getString("modelDescriptor"),
              (root / "models/model.json").string());
    std::filesystem::remove_all(root);
}
