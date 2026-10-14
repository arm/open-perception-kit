/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

#include "Inference.h"

#include <algorithm>
#include <cstdint>
#include <vector>

TEST(OnnxTensor, CreatesUint8OutputTensorWithRawClassIds) {
    const auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
    opk::onnx::Tensor tensor(opk::Shape(1, 2, 2), opk::Dtype::Uint8);
    const std::vector<uint8_t> expected{0U, 18U, 255U, 7U};
    std::copy(expected.begin(), expected.end(), tensor.getData());

    const auto result = tensor.createOnnxTensor(memoryInfo);

    ASSERT_TRUE(result.has_value());
    const auto info = result->GetTensorTypeAndShapeInfo();
    EXPECT_EQ(info.GetElementType(), ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8);
    EXPECT_EQ(info.GetShape(), (std::vector<int64_t>{1, 2, 2}));
    EXPECT_EQ(info.GetElementCount(), expected.size());
    const auto *data = result->GetTensorData<uint8_t>();
    EXPECT_EQ(data, tensor.getData());
    EXPECT_EQ((std::vector<uint8_t>(data, data + expected.size())), expected);
}

TEST(OnnxTensor, ReturnsErrorForUnsupportedType) {
    const auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
    opk::onnx::Tensor tensor(opk::Shape(1), opk::Dtype::Float16);

    const auto result = tensor.createOnnxTensor(memoryInfo);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().flag, opk::ErrorFlag::TensorError);
}

TEST(OnnxTensor, ReturnsErrorWhenRuntimeRejectsTensor) {
    const auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
    opk::onnx::Tensor tensor(opk::Shape(), opk::Dtype::Float32);

    const auto result = tensor.createOnnxTensor(memoryInfo);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().flag, opk::ErrorFlag::TensorError);
}
