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
#pragma once

#include "opk/Shape.h"
#include "opk/Types.h"

#include <mlworkloadlib/workload.hpp>

#include <iterator>
#include <limits>
#include <stdexcept>

namespace opk::mlwl {

struct TensorLayout {
    opk::Shape shape;
    opk::Dtype dtype;
    size_t bytes;
};

inline TensorLayout tensorLayout(mlsdk::workloadlib::ResourceView resource) {
    const auto requirements = resource.requirements();
    if (requirements.kind() != mlsdk::workloadlib::ResourceKind::Tensor ||
        requirements.participatesInAliasing()) {
        throw std::runtime_error("MLWL requires non-aliased tensor inputs and outputs");
    }

    TensorLayout layout;
    switch (requirements.format()) {
    case vk::Format::eR16Sfloat:
        layout.dtype = opk::Dtype::Float16;
        break;
    case vk::Format::eR32Sfloat:
        layout.dtype = opk::Dtype::Float32;
        break;
    default:
        throw std::runtime_error("MLWL inputs and outputs must be Float16 or Float32");
    }
    const auto dimensions = requirements.asTensor().shape();
    const auto strides = requirements.asTensor().stride();
    if (dimensions.empty() || dimensions.size() > std::size(layout.shape.dims) ||
        (!strides.empty() && strides.size() != dimensions.size())) {
        throw std::runtime_error("MLWL tensor rank or stride count is unsupported");
    }

    layout.bytes = opk::getValueTypeByteSize(layout.dtype);
    for (size_t i = dimensions.size(); i-- > 0;) {
        if (dimensions[i] <= 0 || dimensions[i] > std::numeric_limits<int>::max() ||
            static_cast<uint64_t>(dimensions[i]) >
                std::numeric_limits<size_t>::max() / layout.bytes) {
            throw std::runtime_error("MLWL tensor dimensions must be bounded and static");
        }
        if (!strides.empty() && strides[i] != static_cast<int64_t>(layout.bytes)) {
            throw std::runtime_error("MLWL tensor storage must be contiguous");
        }
        layout.shape.dims[i] = static_cast<int>(dimensions[i]);
        layout.bytes *= static_cast<size_t>(dimensions[i]);
    }
    layout.shape.rank = dimensions.size();
    if (requirements.elementCount() != layout.shape.getFullValueCount()) {
        throw std::runtime_error("MLWL tensor storage does not match its shape");
    }
    return layout;
}

} // namespace opk::mlwl
