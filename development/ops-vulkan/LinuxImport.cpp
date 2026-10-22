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

#include "LinuxImport.h"

#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <unistd.h>

namespace opk::vulkan {
namespace {
constexpr int producerWaitMilliseconds{5000};
constexpr VkExternalMemoryHandleTypeFlagBits dmaHandle{
    VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};

bool waitForProducer(const mediaio::DataView &plane) {
    // There is no API to signal a borrowed release fence. Keep that contract on the producer's map
    // path.
    if (plane.sync().hasReleaseFence())
        return false;
    const int fd{plane.sync().hasAcquireFence() ? plane.sync().acquireFenceFd() : plane.fd()};
    pollfd descriptor{fd, POLLIN, 0};
    int result;
    do {
        result = poll(&descriptor, 1, producerWaitMilliseconds);
    } while (result < 0 && errno == EINTR);
    return result > 0 && (descriptor.revents & POLLIN) &&
           !(descriptor.revents & (POLLERR | POLLNVAL));
}
} // namespace

std::unique_ptr<VulkanBuffer> importDmaBuf(const VkPhysicalDevice physicalDevice,
                                           const VkDevice device,
                                           const mediaio::DataView &plane,
                                           const VkDeviceSize maximumBufferBytes) {
    if (!plane.hasDmaBuf() || !plane.canRead())
        return nullptr;
    VkPhysicalDeviceExternalBufferInfo externalInfo{};
    externalInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO;
    externalInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    externalInfo.handleType = dmaHandle;
    VkExternalBufferProperties externalProperties{};
    externalProperties.sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES;
    vkGetPhysicalDeviceExternalBufferProperties(physicalDevice, &externalInfo, &externalProperties);
    if (!(externalProperties.externalMemoryProperties.externalMemoryFeatures &
          VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))
        return nullptr;
    const auto allocationSize{lseek(plane.fd(), 0, SEEK_END)};
    if (allocationSize < 0)
        return nullptr;
    if (lseek(plane.fd(), 0, SEEK_SET) < 0)
        return nullptr;
    constexpr size_t wordBytes{sizeof(uint32_t)};
    const auto allocationBytes{static_cast<VkDeviceSize>(allocationSize)};
    if (allocationBytes == 0 || plane.offset() > allocationBytes ||
        plane.byteSize() > allocationBytes - plane.offset())
        return nullptr;
    const auto planeEnd{static_cast<VkDeviceSize>(plane.offset()) + plane.byteSize()};
    const auto bufferBytes{(planeEnd + wordBytes - 1) / wordBytes * wordBytes};
    if (bufferBytes == 0 || bufferBytes > allocationBytes || bufferBytes > maximumBufferBytes ||
        bufferBytes > std::numeric_limits<uint32_t>::max())
        return nullptr;
    const auto getFdProperties{reinterpret_cast<PFN_vkGetMemoryFdPropertiesKHR>(
        vkGetDeviceProcAddr(device, "vkGetMemoryFdPropertiesKHR"))};
    if (!getFdProperties)
        return nullptr;
    VkMemoryFdPropertiesKHR fdProperties{};
    fdProperties.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;
    const auto propertyResult{getFdProperties(device, dmaHandle, plane.fd(), &fdProperties)};
    if (propertyResult != VK_SUCCESS)
        return nullptr;
    auto buffer{std::make_unique<VulkanBuffer>(device, bufferBytes)};
    VkExternalMemoryBufferCreateInfo externalBuffer{};
    externalBuffer.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
    externalBuffer.handleTypes = dmaHandle;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = &externalBuffer;
    // Describe only the accessed plane range; drivers may require padding beyond it.
    bufferInfo.size = bufferBytes;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer->buffer),
          "Create DMA-BUF import buffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer->buffer, &requirements);
    if (requirements.size > allocationBytes)
        return nullptr;
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    const uint32_t compatibleTypes{requirements.memoryTypeBits & fdProperties.memoryTypeBits};
    uint32_t type{0};
    for (; type < memoryProperties.memoryTypeCount; ++type)
        if (compatibleTypes & (uint32_t{1} << type))
            break;
    if (type == memoryProperties.memoryTypeCount || !waitForProducer(plane))
        return nullptr;
    const int ownedFd{fcntl(plane.fd(), F_DUPFD_CLOEXEC, 0)};
    if (ownedFd < 0)
        return nullptr;
    VkImportMemoryFdInfoKHR importedMemory{};
    importedMemory.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
    importedMemory.handleType = dmaHandle;
    importedMemory.fd = ownedFd;
    VkMemoryDedicatedAllocateInfo dedicated{};
    dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated.buffer = buffer->buffer;
    dedicated.pNext = &importedMemory;
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.pNext = &dedicated;
    allocation.allocationSize = allocationBytes;
    allocation.memoryTypeIndex = type;
    const auto result{vkAllocateMemory(device, &allocation, nullptr, &buffer->memory)};
    if (result != VK_SUCCESS) {
        close(ownedFd);
        if (result == VK_ERROR_INVALID_EXTERNAL_HANDLE)
            return nullptr;
        check(result, "Import DMA-BUF allocation");
    }
    check(vkBindBufferMemory(device, buffer->buffer, buffer->memory, 0), "Bind imported DMA-BUF");
    return buffer;
}
} // namespace opk::vulkan
