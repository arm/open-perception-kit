/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <vector>

#include "Inference.h"

TEST(OnnxTensor, CreatesFloat16TensorOverPekOwnedStorage) {
    pek::onnx::Tensor tensor(pek::Shape(2, 3), pek::Dtype::Float16);
    auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    Ort::Value value = tensor.createOnnxTensor(memoryInfo);
    const auto tensorInfo = value.GetTensorTypeAndShapeInfo();

    EXPECT_EQ(tensor.getByteCount(), 2U * 3U * sizeof(Ort::Float16_t));
    EXPECT_EQ(tensorInfo.GetElementType(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16);
    EXPECT_EQ(tensorInfo.GetShape(), (std::vector<int64_t>{2, 3}));
    EXPECT_EQ(static_cast<const void *>(value.GetTensorData<Ort::Float16_t>()),
              static_cast<const void *>(tensor.getData()));
}

TEST(OnnxTensor, WritesEveryFloat32DescriptorInputValue) {
    pek::onnx::Tensor tensor(pek::Shape(3), pek::Dtype::Float32);

    ASSERT_TRUE(tensor.setValuesFromFloat({1.0F, 2.0F, 3.0F}));

    const auto *values = reinterpret_cast<const float *>(tensor.getData());
    EXPECT_EQ(values[0], 1.0F);
    EXPECT_EQ(values[1], 2.0F);
    EXPECT_EQ(values[2], 3.0F);
}

TEST(OnnxTensor, ConvertsEveryInt64DescriptorInputValue) {
    pek::onnx::Tensor tensor(pek::Shape(3), pek::Dtype::Int64);

    ASSERT_TRUE(tensor.setValuesFromFloat({1.0F, -2.0F, 3.0F}));

    const auto *values = reinterpret_cast<const int64_t *>(tensor.getData());
    EXPECT_EQ(values[0], 1);
    EXPECT_EQ(values[1], -2);
    EXPECT_EQ(values[2], 3);
}

TEST(OnnxTensor, RejectsDescriptorInputValueCountMismatch) {
    pek::onnx::Tensor tensor(pek::Shape(2), pek::Dtype::Int64);

    EXPECT_FALSE(tensor.setValuesFromFloat({1.0F}));
}

TEST(OnnxTensor, RejectsUnrepresentableInt64DescriptorInputValue) {
    pek::onnx::Tensor tensor(pek::Shape(1), pek::Dtype::Int64);

    EXPECT_FALSE(tensor.setValuesFromFloat({std::numeric_limits<float>::infinity()}));
}
