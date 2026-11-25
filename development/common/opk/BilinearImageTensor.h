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

#include "opk/ImageOpDesc.h"
#include "opk/Model.h"
#include <span>

namespace opk {
struct BilinearImageTensor {
    std::vector<float> pixels;
    size_t left = 0;
    size_t top = 0;
    size_t width = 0;
    size_t height = 0;
};

/// Centered fixed-canvas letterboxing: uint8 half-pixel bilinear resize, then normalization.
/// Supports RGB/BGRA input and Float32 RGB/BGR CHW or RGB HWC output; no auto-stride padding.
Result<BilinearImageTensor> buildBilinearImageTensor(const std::span<const uint8_t> pixels,
                                                     const size_t width,
                                                     const size_t height,
                                                     const size_t stride,
                                                     const RawImagePixelFormat format,
                                                     const ModelInput &input);
} // namespace opk
