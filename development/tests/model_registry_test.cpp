/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <map>
#include <string>

#include "model_reg.h"

TEST(ModelRegistry, ReportsOptionalDisplayMetadata) {
    ModelRegistry registry;
    registry.add_model("YoloV11", "pekinfer0", true, "YOLOv11n", "Object detection", "ONNX");

    const auto report = registry.report();

    ASSERT_EQ(report.size(), 1);
    EXPECT_EQ(report[0].at("name"), "YoloV11");
    EXPECT_EQ(report[0].at("element_name"), "pekinfer0");
    EXPECT_EQ(report[0].at("active"), true);
    EXPECT_EQ(report[0].at("displayName"), "YOLOv11n");
    EXPECT_EQ(report[0].at("task"), "Object detection");
    EXPECT_EQ(report[0].at("runtime"), "ONNX");
}

TEST(ModelRegistry, OmitsAbsentDisplayMetadata) {
    ModelRegistry registry;
    registry.add_model("Custom", "pekinfer0", false);

    const auto report = registry.report();

    ASSERT_EQ(report.size(), 1);
    EXPECT_EQ(report[0].at("name"), "Custom");
    EXPECT_EQ(report[0].at("element_name"), "pekinfer0");
    EXPECT_EQ(report[0].at("active"), false);
    EXPECT_FALSE(report[0].contains("displayName"));
    EXPECT_FALSE(report[0].contains("task"));
    EXPECT_FALSE(report[0].contains("runtime"));
}

TEST(ModelRegistry, KeepsRuntimeMetadataForDuplicateInternalNames) {
    ModelRegistry registry;
    registry.add_model(
        "ImageNet", "pekinfer-onnx", false, "MobileNetV2", "Image classification", "ONNX");
    registry.add_model("ImageNet",
                       "pekinfer-executorch",
                       false,
                       "MobileNetV2",
                       "Image classification",
                       "ExecuTorch");

    std::map<std::string, std::string, std::less<>> runtimes;
    for (const auto &model : registry.report()) {
        runtimes[model.at("element_name").get<std::string>()] =
            model.at("runtime").get<std::string>();
    }

    EXPECT_EQ(runtimes.at("pekinfer-onnx"), "ONNX");
    EXPECT_EQ(runtimes.at("pekinfer-executorch"), "ExecuTorch");
}
