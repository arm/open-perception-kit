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

TEST(OnnxTensor, ReturnsErrorForUnsupportedType) {
    const auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
    opk::onnx::Tensor tensor(opk::Shape(1), opk::Dtype::Float16);

    const auto result = tensor.createOnnxTensor(memoryInfo);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().flag, opk::ErrorFlag::TensorError);
    uint8_t *const originalData{tensor.getData()};
    constexpr size_t tensorElements{1};
    auto storage{std::make_shared<std::vector<uint16_t>>(tensorElements)};
    const auto binding{tensor.bindStorage(
        memoryInfo,
        {reinterpret_cast<uint8_t *>(storage->data()), tensorElements * sizeof(uint16_t)},
        storage)};
    ASSERT_FALSE(binding.has_value());
    EXPECT_EQ(tensor.getData(), originalData);
    EXPECT_FALSE(tensor.hasSharedStorage());
}

TEST(OnnxTensor, HandlesRuntimeRejectionAndSharedStorage) {
    const auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
    opk::onnx::Tensor tensor(opk::Shape(), opk::Dtype::Float32);

    const auto result = tensor.createOnnxTensor(memoryInfo);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().flag, opk::ErrorFlag::TensorError);

    constexpr int tensorElements{4};
    constexpr size_t tensorBytes{tensorElements * sizeof(float)};
    constexpr float tensorValue{0.5F};
    constexpr size_t misalignmentBytes{1};
    opk::onnx::Tensor input(opk::Shape(tensorElements), opk::Dtype::Float32);
    uint8_t *const originalData{input.getData()};
    auto storage{std::make_shared<std::vector<float>>(tensorElements, tensorValue)};
    const std::span<uint8_t> sharedBytes{reinterpret_cast<uint8_t *>(storage->data()), tensorBytes};
    EXPECT_FALSE(input.bindStorage(memoryInfo, sharedBytes, nullptr));
    EXPECT_FALSE(
        input.bindStorage(memoryInfo, sharedBytes.first(tensorBytes - sizeof(float)), storage));
    std::vector<uint8_t> misalignedStorage(tensorBytes + misalignmentBytes);
    EXPECT_FALSE(input.bindStorage(
        memoryInfo, {misalignedStorage.data() + misalignmentBytes, tensorBytes}, storage));
    EXPECT_EQ(input.getData(), originalData);
    EXPECT_FALSE(input.hasSharedStorage());

    const std::weak_ptr<void> firstOwner{storage};
    auto bound{input.bindStorage(memoryInfo, sharedBytes, storage)};
    ASSERT_TRUE(bound.has_value());
    EXPECT_EQ(bound->GetTensorData<float>(), storage->data());
    EXPECT_EQ(input.getData(), sharedBytes.data());
    EXPECT_EQ(bound->GetTensorData<float>()[tensorElements - 1], tensorValue);
    storage.reset();
    EXPECT_FALSE(firstOwner.expired());

    auto nextStorage{std::make_shared<std::vector<float>>(tensorElements, tensorValue)};
    const std::weak_ptr<void> nextOwner{nextStorage};
    const std::span<uint8_t> nextBytes{reinterpret_cast<uint8_t *>(nextStorage->data()),
                                       tensorBytes};
    bound = input.bindStorage(memoryInfo, nextBytes, nextStorage);
    ASSERT_TRUE(bound.has_value());
    EXPECT_TRUE(firstOwner.expired());
    nextStorage.reset();
    EXPECT_FALSE(nextOwner.expired());
    EXPECT_FALSE(input.bindStorage(memoryInfo, nextBytes, nullptr));
    EXPECT_EQ(input.getData(), nextBytes.data());

    bound = input.resetStorage(memoryInfo);
    ASSERT_TRUE(bound.has_value());
    EXPECT_TRUE(nextOwner.expired());
    EXPECT_FALSE(input.hasSharedStorage());
    uint8_t *const ownedData{input.getData()};
    EXPECT_EQ(bound->GetTensorData<float>(), reinterpret_cast<float *>(ownedData));
    bound = input.resetStorage(memoryInfo);
    ASSERT_TRUE(bound.has_value());
    EXPECT_EQ(input.getData(), ownedData);
}
