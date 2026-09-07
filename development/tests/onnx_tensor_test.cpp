/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include "Inference.h"

TEST(OnnxTensor, ReturnsErrorForUnsupportedType) {
    const auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
    pek::onnx::Tensor tensor(pek::Shape(1), pek::Dtype::Float16);

    const auto result = tensor.createOnnxTensor(memoryInfo);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().flag, pek::ErrorFlag::TensorError);
}

TEST(OnnxTensor, ReturnsErrorWhenRuntimeRejectsTensor) {
    const auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
    pek::onnx::Tensor tensor(pek::Shape(), pek::Dtype::Float32);

    const auto result = tensor.createOnnxTensor(memoryInfo);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().flag, pek::ErrorFlag::TensorError);
}
