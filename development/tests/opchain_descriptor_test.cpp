/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include "op/OpChain.h"
#include "op/OpChainDescriptor.h"

TEST(OpChainDescriptor, ParsesOptionalDisplayMetadata) {
    auto descriptor = pek::op::OpChainDescriptor::fromJson(
        R"({"name":"YoloV11","displayName":"YOLOv11n","task":"Object detection","runtime":"ONNX","ops":[]})");

    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->name, "YoloV11");
    EXPECT_EQ(descriptor->displayName, "YOLOv11n");
    EXPECT_EQ(descriptor->task, "Object detection");
    EXPECT_EQ(descriptor->runtime, "ONNX");
}

TEST(OpChainDescriptor, LeavesMissingDisplayMetadataEmpty) {
    auto descriptor = pek::op::OpChainDescriptor::fromJson(R"({"name":"Custom","ops":[]})");

    ASSERT_TRUE(descriptor.has_value());
    EXPECT_TRUE(descriptor->displayName.empty());
    EXPECT_TRUE(descriptor->task.empty());
    EXPECT_TRUE(descriptor->runtime.empty());
}

TEST(OpChainDescriptor, RejectsInvalidDisplayMetadataTypes) {
    auto descriptor = pek::op::OpChainDescriptor::fromJson(
        R"({"name":"Invalid","displayName":42,"task":"Detection","runtime":"ONNX","ops":[]})");

    EXPECT_FALSE(descriptor.has_value());
}

TEST(OpChainDescriptor, SerializesOnlyPopulatedDisplayMetadata) {
    pek::op::OpChainDescriptor descriptor;
    descriptor.name = "Custom";
    descriptor.displayName = "Custom model";
    descriptor.runtime = "CustomRT";

    const nlohmann::json serialized = descriptor;

    EXPECT_EQ(serialized.at("name"), "Custom");
    EXPECT_EQ(serialized.at("displayName"), "Custom model");
    EXPECT_EQ(serialized.at("runtime"), "CustomRT");
    EXPECT_FALSE(serialized.contains("task"));
}

TEST(OpChain, RetainsDisplayMetadataFromDescriptor) {
    pek::op::OpChainDescriptor descriptor;
    descriptor.name = "YoloV11";
    descriptor.displayName = "YOLOv11n";
    descriptor.task = "Object detection";
    descriptor.runtime = "ONNX";

    pek::op::OpChain opChain;
    auto setup = opChain.setupFromDescriptor(descriptor);

    ASSERT_TRUE(setup.has_value());
    EXPECT_EQ(opChain.getName(), "YoloV11");
    EXPECT_EQ(opChain.getDisplayName(), "YOLOv11n");
    EXPECT_EQ(opChain.getTask(), "Object detection");
    EXPECT_EQ(opChain.getRuntime(), "ONNX");
}
