/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

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
