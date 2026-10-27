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
#include "TensorLayout.h"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

namespace {
namespace wl = mlsdk::workloadlib;

wl::Workload
tensorWorkload(vk::Format format, std::vector<int64_t> shape, std::vector<int64_t> strides = {}) {
    wl::DataGraphDescription graph;
    wl::DataGraphResource input;
    input.name = "input";
    input.access = wl::ResourceAccess::Read;
    input.resource.kind = wl::ResourceKind::Tensor;
    input.resource.format = format;
    input.resource.tensor.shape = std::move(shape);
    input.resource.tensor.stride = std::move(strides);
    graph.resources.push_back(std::move(input));
    return wl::Workload::fromDataGraph(std::move(graph));
}

TEST(MlwlTensor, AcceptsContiguousFloatingPointTensors) {
    for (const auto format : {vk::Format::eR16Sfloat, vk::Format::eR32Sfloat}) {
        const auto workload = tensorWorkload(format, {2, 3});
        const auto layout = opk::mlwl::tensorLayout(workload.resource(0));
        EXPECT_EQ(layout.shape, opk::Shape(2, 3));
        EXPECT_EQ(layout.dtype,
                  format == vk::Format::eR16Sfloat ? opk::Dtype::Float16 : opk::Dtype::Float32);
        EXPECT_EQ(layout.bytes, 6 * opk::getValueTypeByteSize(layout.dtype));
    }
    const auto workload = tensorWorkload(vk::Format::eR32Sfloat, {2, 3}, {12, 4});
    EXPECT_EQ(opk::mlwl::tensorLayout(workload.resource(0)).bytes, 24);
}

TEST(MlwlTensor, RejectsUnsupportedTypesAndLayouts) {
    const auto integer = tensorWorkload(vk::Format::eR8Sint, {2, 3});
    EXPECT_THROW(opk::mlwl::tensorLayout(integer.resource(0)), std::runtime_error);
    const auto padded = tensorWorkload(vk::Format::eR32Sfloat, {2, 3}, {16, 4});
    EXPECT_THROW(opk::mlwl::tensorLayout(padded.resource(0)), std::runtime_error);
    const auto rank = tensorWorkload(vk::Format::eR32Sfloat, std::vector<int64_t>(9, 1));
    EXPECT_THROW(opk::mlwl::tensorLayout(rank.resource(0)), std::runtime_error);
    const auto dimension = tensorWorkload(
        vk::Format::eR32Sfloat, {static_cast<int64_t>(std::numeric_limits<int>::max()) + 1});
    EXPECT_THROW(opk::mlwl::tensorLayout(dimension.resource(0)), std::runtime_error);
}

} // namespace
