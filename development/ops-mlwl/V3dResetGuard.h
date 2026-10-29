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

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>

namespace vk::raii {
class PhysicalDevice;
}

namespace opk::mlwl {

class V3dReset : public std::runtime_error {
  public:
    V3dReset() : std::runtime_error("V3D GPU reset detected; inference output is invalid") {}
};

class V3dResetGuard {
  public:
    using ReadCounter = std::function<uint64_t()>;

    static std::unique_ptr<V3dResetGuard> create(const vk::raii::PhysicalDevice &device);
    explicit V3dResetGuard(ReadCounter readCounter);
    ~V3dResetGuard();

    V3dResetGuard(const V3dResetGuard &) = delete;
    V3dResetGuard &operator=(const V3dResetGuard &) = delete;

    void check() const;

  private:
    ReadCounter readCounter;
    const uint64_t generation;
    int descriptor = -1;
};

} // namespace opk::mlwl
