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

#include <fmt/format.h>
#include <memory>
#include <stdexcept>
#include <vulkan/vulkan.h>

namespace opk::vulkan {
inline void require(const bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
inline void check(const VkResult result, const char *const operation) {
    require(result == VK_SUCCESS, fmt::format("{} failed (Vulkan {})", operation, int(result)));
}

/** Buffer allocations never outlive the device owned by GpuPreprocess. */
struct VulkanBuffer {
    const VkDevice device;
    VkBuffer buffer{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    const VkDeviceSize size;
    void *mapped{nullptr};
    bool coherent{false};

    VulkanBuffer(const VkDevice device, const VkDeviceSize size) : device{device}, size{size} {}
    ~VulkanBuffer() {
        if (mapped)
            vkUnmapMemory(device, memory);
        if (buffer)
            vkDestroyBuffer(device, buffer, nullptr);
        if (memory)
            vkFreeMemory(device, memory, nullptr);
    }
    VulkanBuffer(const VulkanBuffer &) = delete;
    VulkanBuffer &operator=(const VulkanBuffer &) = delete;

    void flush() const {
        if (coherent)
            return;
        VkMappedMemoryRange range{};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = memory;
        range.size = VK_WHOLE_SIZE;
        check(vkFlushMappedMemoryRanges(device, 1, &range), "Flush GPU upload");
    }
    void invalidate() const {
        if (coherent)
            return;
        VkMappedMemoryRange range{};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = memory;
        range.size = VK_WHOLE_SIZE;
        check(vkInvalidateMappedMemoryRanges(device, 1, &range), "Invalidate tensor mapping");
    }
};
} // namespace opk::vulkan
