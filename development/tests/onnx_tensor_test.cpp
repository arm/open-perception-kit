/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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
