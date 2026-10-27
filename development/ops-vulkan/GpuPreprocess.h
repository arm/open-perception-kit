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

#include "mediaio/VideoFrame.h"
#include "opk/ImageOpDesc.h"
#include "opk/Model.h"
#include <memory>
#include <span>
#include <string>

namespace opk::vulkan {

/** Serial compute preprocessing; its mapped tensor owns the Vulkan device until released. */
class GpuPreprocess {
  public:
    GpuPreprocess(const size_t tensorBytes, const bool tensorReadback);
    ~GpuPreprocess();
    GpuPreprocess(const GpuPreprocess &) = delete;
    GpuPreprocess &operator=(const GpuPreprocess &) = delete;

    /** Import without CPU mapping. A false result allows the caller to obtain a readable view. */
    bool import(const mediaio::VideoFrame &frame);
    /** Upload raw bytes only; image transformations remain in the compute shader. */
    void upload(const mediaio::VideoFrame &frame);
    /** Finish GPU access before returning the CPU-readable tensor. */
    std::span<uint8_t> run(const mediaio::VideoFrame &frame,
                           const PixelRect &crop,
                           const PixelRect &inner,
                           const ModelInput &input);
    std::string identity() const;
    bool tensorCopied() const;

  private:
    struct State;
    std::unique_ptr<State> state;
};
} // namespace opk::vulkan
