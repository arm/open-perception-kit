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

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "model_manager.h"

TEST(ModelManager, ReportsOptionalDisplayMetadata) {
    gst_init(nullptr, nullptr);
    g_autoptr(GstElement) sink = gst_element_factory_make("fakesink", nullptr);
    ModelManager manager(sink);
    const ModelData notification{
        .m_elementName = "opkinfer0",
        .m_metadata = {"YoloV11", "YOLOv11n", "Object detection", "ONNX"},
        .m_interface = {.m_requiredInputs = {{"consumer",
                                              "input",
                                              "open_perception_kit.metadata.ObjectEmbeddings",
                                              "embed/output"}},
                        .m_providedOutputs = {{"open_perception_kit.metadata.BoxDetections",
                                               "detector/output"}},
                        .m_upstreamInputProviders = {}},
        .m_active = true};
    manager.registerModel(notification);
    manager.registerModel(notification);
    const auto snapshot = manager.getModelGroups();
    ASSERT_EQ(snapshot.m_modelGroups.size(), 1);
    const auto &model = snapshot.m_modelGroups.front().getModels().front();
    EXPECT_EQ(model.getMetadata().m_name, "YoloV11");
    EXPECT_EQ(model.getMetadata().m_runtime, "ONNX");
    EXPECT_EQ(model.getElementName(), "opkinfer0");
    EXPECT_TRUE(model.isActive());
    EXPECT_EQ(model.getMetadata().m_displayName, "YOLOv11n");
    EXPECT_EQ(model.getMetadata().m_task, "Object detection");
    EXPECT_EQ(model.getInterface().m_providedOutputs, notification.m_interface.m_providedOutputs);
    EXPECT_EQ(model.getRequiredInputs(), notification.m_interface.m_requiredInputs);
    EXPECT_FALSE(model.isConfigured());
    EXPECT_TRUE(model.getOpChainPath().empty());

    manager.registerModel({.m_elementName = "opkinfer0",
                           .m_metadata = {"Replacement", {}, {}, {}},
                           .m_interface = {}});
    const auto replacementSnapshot = manager.getModelGroups();
    const auto &replacement = replacementSnapshot.m_modelGroups.front().getModels().front();
    EXPECT_EQ(replacement.getMetadata().m_name, "Replacement");
    EXPECT_FALSE(replacement.isActive());
    EXPECT_FALSE(replacementSnapshot.m_modelGroups.front().isEditable());
    EXPECT_TRUE(replacement.getInterface().m_requiredInputs.empty());
    EXPECT_TRUE(replacement.getInterface().m_providedOutputs.empty());
    EXPECT_EQ(model.getMetadata().m_name, "YoloV11");
}

TEST(ModelManager, OmitsAbsentDisplayMetadata) {
    gst_init(nullptr, nullptr);
    g_autoptr(GstElement) sink = gst_element_factory_make("fakesink", nullptr);
    ModelManager manager(sink);
    const ModelData modelData{
        .m_elementName = "opkinfer0", .m_metadata = {"Custom", {}, {}, {}}, .m_interface = {}};
    manager.registerModel(modelData);
    const auto snapshot = manager.getModelGroups();
    ASSERT_EQ(snapshot.m_modelGroups.size(), 1);
    const auto &model = snapshot.m_modelGroups.front().getModels().front();
    EXPECT_EQ(model.getMetadata().m_name, "Custom");
    EXPECT_EQ(model.getElementName(), "opkinfer0");
    EXPECT_FALSE(model.isActive());
    EXPECT_TRUE(model.getMetadata().m_displayName.empty());
    EXPECT_TRUE(model.getMetadata().m_task.empty());
    EXPECT_TRUE(model.getMetadata().m_runtime.empty());
    EXPECT_TRUE(model.getInterface().m_providedOutputs.empty());
    EXPECT_TRUE(model.getRequiredInputs().empty());
    EXPECT_FALSE(model.isConfigured());
    EXPECT_TRUE(model.getOpChainPath().empty());
}

TEST(ModelManager, KeepsRuntimeMetadataForDuplicateInternalNames) {
    gst_init(nullptr, nullptr);
    g_autoptr(GstElement) sink = gst_element_factory_make("fakesink", nullptr);
    ModelManager manager(sink);
    manager.registerModel(
        {.m_elementName = "opkinfer-onnx",
         .m_metadata = {"ImageNet", "MobileNetV2", "Image classification", "ONNX"},
         .m_interface = {}});
    manager.registerModel(
        {.m_elementName = "opkinfer-executorch",
         .m_metadata = {"ImageNet", "MobileNetV2", "Image classification", "ExecuTorch"},
         .m_interface = {}});

    std::map<std::string, std::string, std::less<>> runtimes;
    const auto snapshot = manager.getModelGroups();
    for (const auto &group : snapshot.m_modelGroups) {
        for (const auto &model : group.getModels()) {
            runtimes[model.getElementName()] = model.getMetadata().m_runtime;
        }
    }
    EXPECT_EQ(runtimes.at("opkinfer-onnx"), "ONNX");
    EXPECT_EQ(runtimes.at("opkinfer-executorch"), "ExecuTorch");

    // Different bins may contain elements with the same name.
    const std::shared_ptr<GstElement> firstElement(
        gst_element_factory_make("identity", "shared-model"), gst_object_unref);
    const std::shared_ptr<GstElement> secondElement(
        gst_element_factory_make("identity", "shared-model"), gst_object_unref);
    ASSERT_NE(firstElement, nullptr);
    ASSERT_NE(secondElement, nullptr);
    const ModelData firstData{
        .m_elementName = "shared-model",
        .m_metadata = {"NITEC", "NITEC", "Camera contact", "ONNX"},
        .m_interface = {.m_requiredInputs = {},
                        .m_providedOutputs = {{"open_perception_kit.metadata.ObjectEmbeddings",
                                               "first/output"}},
                        .m_upstreamInputProviders = {}}};
    const ModelData secondData{
        .m_elementName = "shared-model",
        .m_metadata = {"YoloV11", "YOLOv11n", "Object detection", "ONNX"},
        .m_interface = {
            .m_requiredInputs = {{"consumer", "input", "payload", "source/output"}},
            .m_providedOutputs = {{"open_perception_kit.metadata.BoxDetections", "second/output"}},
            .m_upstreamInputProviders = {{"payload", "source/output"}}}};
    Model firstModel(firstElement, firstData.m_metadata);
    firstModel.updateModelData(firstData);
    Model secondModel(secondElement, secondData.m_metadata);
    secondModel.updateModelData(secondData);
    const std::vector modelGroups{ModelGroup(firstModel), ModelGroup(secondModel)};
    ModelGroup editedGroup(Model(secondElement, {}));
    editedGroup.refreshRuntimeDataFrom(modelGroups, nullptr);
    const auto &retainedModel = editedGroup.getModels().front();
    EXPECT_EQ(retainedModel.getElement(), secondElement);
    EXPECT_EQ(retainedModel.getMetadata().m_name, secondData.m_metadata.m_name);
    EXPECT_EQ(retainedModel.getMetadata().m_task, secondData.m_metadata.m_task);
    EXPECT_EQ(retainedModel.getInterface().m_requiredInputs,
              secondData.m_interface.m_requiredInputs);
    EXPECT_EQ(retainedModel.getInterface().m_providedOutputs,
              secondData.m_interface.m_providedOutputs);
    EXPECT_EQ(retainedModel.getInterface().m_upstreamInputProviders,
              secondData.m_interface.m_upstreamInputProviders);
}
