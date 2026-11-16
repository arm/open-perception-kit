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

#include <gtest/gtest.h>

#include <array>
#include <map>
#include <string>

#include "gst/ResultRequirementEvent.h"
#include "model_reg.h"

TEST(ModelRegistry, ReportsOptionalDisplayMetadata) {
    ModelRegistry registry;
    registry.add_model({
        .name = "YoloV11",
        .active = true,
        .element_name = "opkinfer0",
        .display_name = "YOLOv11n",
        .task = "Object detection",
        .runtime = "ONNX",
        .provided_outputs = {{"open_perception_kit.metadata.BoxDetections", "genericObject"}},
        .required_outputs = {{"open_perception_kit.metadata.ObjectEmbeddings", "imageEmbedding"}},
    });

    const auto report = registry.report();

    ASSERT_EQ(report.size(), 1);
    EXPECT_EQ(report[0].at("name"), "YoloV11");
    EXPECT_EQ(report[0].at("element_name"), "opkinfer0");
    EXPECT_EQ(report[0].at("active"), true);
    EXPECT_EQ(report[0].at("displayName"), "YOLOv11n");
    EXPECT_EQ(report[0].at("task"), "Object detection");
    EXPECT_EQ(report[0].at("runtime"), "ONNX");
    EXPECT_EQ(report[0].at("providedOutputs"),
              nlohmann::json::array({{{"payloadType", "open_perception_kit.metadata.BoxDetections"},
                                      {"role", "genericObject"}}}));
    EXPECT_EQ(
        report[0].at("requiredOutputs"),
        nlohmann::json::array({{{"payloadType", "open_perception_kit.metadata.ObjectEmbeddings"},
                                {"role", "imageEmbedding"}}}));
}

TEST(ModelRegistry, OmitsAbsentDisplayMetadata) {
    ModelRegistry registry;
    registry.add_model({.name = "Custom", .active = false, .element_name = "opkinfer0"});

    const auto report = registry.report();

    ASSERT_EQ(report.size(), 1);
    EXPECT_EQ(report[0].at("name"), "Custom");
    EXPECT_EQ(report[0].at("element_name"), "opkinfer0");
    EXPECT_EQ(report[0].at("active"), false);
    EXPECT_FALSE(report[0].contains("displayName"));
    EXPECT_FALSE(report[0].contains("task"));
    EXPECT_FALSE(report[0].contains("runtime"));
    EXPECT_EQ(report[0].at("providedOutputs"), nlohmann::json::array());
    EXPECT_EQ(report[0].at("requiredOutputs"), nlohmann::json::array());
}

TEST(ModelRegistry, KeepsRuntimeMetadataForDuplicateInternalNames) {
    ModelRegistry registry;
    registry.add_model({.name = "ImageNet",
                        .active = false,
                        .element_name = "opkinfer-onnx",
                        .display_name = "MobileNetV2",
                        .task = "Image classification",
                        .runtime = "ONNX"});
    registry.add_model({.name = "ImageNet",
                        .active = false,
                        .element_name = "opkinfer-executorch",
                        .display_name = "MobileNetV2",
                        .task = "Image classification",
                        .runtime = "ExecuTorch"});

    std::map<std::string, std::string, std::less<>> runtimes;
    for (const auto &model : registry.report()) {
        runtimes[model.at("element_name").get<std::string>()] =
            model.at("runtime").get<std::string>();
    }

    EXPECT_EQ(runtimes.at("opkinfer-onnx"), "ONNX");
    EXPECT_EQ(runtimes.at("opkinfer-executorch"), "ExecuTorch");
}

TEST(ModelRegistration, ParsesOptionalFieldsAndTypedOutputs) {
    gst_init(nullptr, nullptr);
    const std::vector<opk::OutputCapability> provided{
        {"open_perception_kit.metadata.BoxDetections", "genericObject"},
        {"open_perception_kit.metadata.BoxDetections", "humanFace"}};
    const std::vector<opk::OutputCapability> required{
        {"open_perception_kit.metadata.ObjectEmbeddings", "imageEmbedding"}};
    GstStructure *structure = gst_structure_new("opk-model-register",
                                                "model-name",
                                                G_TYPE_STRING,
                                                "YoloV11",
                                                "element-name",
                                                G_TYPE_STRING,
                                                "opkinfer0",
                                                "active",
                                                G_TYPE_BOOLEAN,
                                                TRUE,
                                                "display-name",
                                                G_TYPE_STRING,
                                                "YOLOv11n",
                                                "task",
                                                G_TYPE_STRING,
                                                "Object detection",
                                                "runtime",
                                                G_TYPE_STRING,
                                                "ONNX",
                                                nullptr);
    opk::result_requirement_event::writeList(structure, "provided-outputs", provided);
    opk::result_requirement_event::writeList(structure, "required-outputs", required);

    const auto status = model_status_from_registration(structure);

    ASSERT_TRUE(status);
    if (!status)
        return;
    EXPECT_EQ(status->name, "YoloV11");
    EXPECT_TRUE(status->active);
    EXPECT_EQ(status->display_name, "YOLOv11n");
    EXPECT_EQ(status->task, "Object detection");
    EXPECT_EQ(status->runtime, "ONNX");
    EXPECT_EQ(status->provided_outputs, provided);
    EXPECT_EQ(status->required_outputs, required);
    gst_structure_free(structure);
}

TEST(ModelRegistration, DoesNotInterpretMalformedCapabilityLists) {
    gst_init(nullptr, nullptr);
    GstStructure *structure = gst_structure_new("opk-model-register",
                                                "model-name",
                                                G_TYPE_STRING,
                                                "Custom",
                                                "element-name",
                                                G_TYPE_STRING,
                                                "opkinfer0",
                                                "provided-outputs",
                                                G_TYPE_STRING,
                                                "not-a-string-vector",
                                                nullptr);
    GValue null_content_types = G_VALUE_INIT;
    g_value_init(&null_content_types, G_TYPE_STRV);
    g_value_set_boxed(&null_content_types, nullptr);
    gst_structure_set_value(structure, "required-outputs", &null_content_types);

    const auto status = model_status_from_registration(structure);

    ASSERT_TRUE(status);
    if (!status)
        return;
    EXPECT_FALSE(status->active);
    EXPECT_TRUE(status->display_name.empty());
    EXPECT_TRUE(status->task.empty());
    EXPECT_TRUE(status->runtime.empty());
    EXPECT_TRUE(status->provided_outputs.empty());
    EXPECT_TRUE(status->required_outputs.empty());
    g_value_unset(&null_content_types);
    gst_structure_free(structure);
}

TEST(ModelRegistration, RejectsMissingRequiredNames) {
    GstStructure *structure = gst_structure_new_empty("opk-model-register");
    EXPECT_FALSE(model_status_from_registration(structure));

    gst_structure_set(structure, "model-name", G_TYPE_STRING, "Custom", nullptr);
    EXPECT_FALSE(model_status_from_registration(structure));
    gst_structure_free(structure);
}
