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

#include "Inference.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>

namespace {
class OnnxDynamicInput : public testing::Test {
  protected:
    std::filesystem::path assets;
    void SetUp() override {
        const char *const directory = std::getenv("OPK_QUERY_TEST_ASSETS");
        if (!directory) {
            GTEST_SKIP() << "Set OPK_QUERY_TEST_ASSETS to qualified dynamic ONNX fixtures";
        }
        assets = directory;
    }
};
} // namespace

TEST_F(OnnxDynamicInput, DynamicShapeChangesAreAtomicAndRespectGraphAndByteBounds) {
    opk::onnx::Inference inference;
    const auto setup = inference.setupFromJson((assets / "model.json").string(), 2, 1);
    ASSERT_TRUE(setup) << setup.error().info;
    constexpr size_t MaxBytes = 16 * 512 * sizeof(float);
    for (const int groups : {1, 16, 1}) {
        ASSERT_TRUE(inference.resizeInputTensor(
            1, opk::Shape(1, groups, 512), opk::Dtype::Float32, MaxBytes));
        EXPECT_EQ(inference.getModel().inputs[1].shape, opk::Shape(1, groups, 512));
    }
    auto owner = std::make_shared<std::vector<float>>(512, 0.25F);
    ASSERT_TRUE(inference.setSharedInputStorage(
        1, {reinterpret_cast<uint8_t *>(owner->data()), owner->size() * sizeof(float)}, owner));
    const auto *const bound = inference.getInputTensorDataAddress(1);
    for (const auto &shape : {opk::Shape(2, 1, 512),
                              opk::Shape(1, 0, 512),
                              opk::Shape(1, 1, 513),
                              opk::Shape(1, INT_MAX, 512)}) {
        EXPECT_FALSE(inference.resizeInputTensor(1, shape, opk::Dtype::Float32, MaxBytes));
        EXPECT_EQ(inference.getInputTensorDataAddress(1), bound);
        EXPECT_EQ(inference.getModel().inputs[1].shape, opk::Shape(1, 1, 512));
    }
    EXPECT_FALSE(
        inference.resizeInputTensor(1, opk::Shape(1, 1, 512), opk::Dtype::Int64, MaxBytes));
    EXPECT_FALSE(
        inference.resizeInputTensor(99, opk::Shape(1, 1, 512), opk::Dtype::Float32, MaxBytes));
    EXPECT_FALSE(inference.resizeInputTensor(1, opk::Shape(1, 1, 512), opk::Dtype::Float32, 0));
    EXPECT_EQ(inference.getInputTensorDataAddress(1), bound);
    ASSERT_TRUE(inference.setOwnedInputStorage(1));
    EXPECT_NE(inference.getInputTensorDataAddress(1), bound);
    ASSERT_TRUE(
        inference.resizeInputTensor(1, opk::Shape(1, 16, 512), opk::Dtype::Float32, MaxBytes));
}

TEST_F(OnnxDynamicInput, EncodersUseSharedBackendWithDynamicGroupsAndOriginalGraphMetadata) {
    for (const bool visual : {false, true}) {
        opk::onnx::Inference backend;
        const auto setup = backend.setupFromJson(
            (assets / (visual ? "model-visual-encoder.json" : "model-text-encoder.json")).string(),
            2,
            1);
        ASSERT_TRUE(setup) << setup.error().info;
        const size_t index = visual ? 1 : 0;
        const auto declaredShape = visual ? opk::Shape(1, -1, 80, 80) : opk::Shape(-1, 77);
        for (const int groups : {1, 4, 16, 1}) {
            const auto shape = visual ? opk::Shape(1, groups, 80, 80) : opk::Shape(groups, 77);
            const auto resized =
                backend.resizeInputTensor(index,
                                          shape,
                                          visual ? opk::Dtype::Float32 : opk::Dtype::Int64,
                                          16 * 80 * 80 * sizeof(float));
            ASSERT_TRUE(resized) << resized.error().info;
            EXPECT_EQ(backend.getDeclaredModel().inputs[index].shape, declaredShape);
            EXPECT_EQ(backend.getModel().inputs[index].shape, shape);
            if (visual) {
                std::memset(backend.getInputTensorDataAddress(0), 0, 3 * 640 * 640 * sizeof(float));
            }
            const size_t bytes =
                visual ? groups * 80 * 80 * sizeof(float) : groups * 77 * sizeof(int64_t);
            std::memset(backend.getInputTensorDataAddress(index), 0, bytes);
            const auto inferred = backend.inference();
            ASSERT_TRUE(inferred) << inferred.error().info;
            EXPECT_EQ(backend.getOutputTensorFinalShape(0), opk::Shape(1, groups, 512));
            const auto *const data =
                reinterpret_cast<const float *>(backend.getOutputTensorDataAddress(0));
            EXPECT_TRUE(std::all_of(
                data, data + groups * 512, [](const float value) { return std::isfinite(value); }));
        }
    }
}
