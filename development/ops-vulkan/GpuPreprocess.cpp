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

#include "GpuPreprocess.h"
#include "VulkanBuffer.h"
#include "preprocess_spirv.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace opk::vulkan {
namespace {
constexpr uint32_t workgroupSide{8};
constexpr size_t sourcePlaneCount{3};
constexpr size_t storageBindingCount{sourcePlaneCount + 1};
constexpr size_t descriptorBindingCount{storageBindingCount + 1};
constexpr uint32_t tensorBinding{sourcePlaneCount};
constexpr uint32_t parameterBinding{storageBindingCount};
constexpr uint64_t gpuWaitNanoseconds{5'000'000'000};
constexpr VkDeviceSize wordBytes{sizeof(uint32_t)};
constexpr uint32_t chromaSubsampling{2};
constexpr uint32_t bgraPixelBytes{4};
constexpr float byteMaximum{255.0F};
constexpr float limitedLumaOffset{16.0F};
constexpr float limitedLumaRange{219.0F};
constexpr float chromaCenter{128.0F};
constexpr float limitedChromaRange{224.0F};
constexpr float normalizationEpsilon{1e-12F};
constexpr float chromaCoefficientScale{2.0F};
enum class SourceFormat : uint32_t { Bgra, Nv12, I420, Yuy2 };

struct VulkanInstance {
    VkInstance handle{VK_NULL_HANDLE};
    std::vector<VkPhysicalDevice> devices;
    VulkanInstance() {
        VkApplicationInfo application{};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pApplicationName = "OPK preprocessing";
        application.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo instanceInfo{};
        instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.pApplicationInfo = &application;
        check(vkCreateInstance(&instanceInfo, nullptr, &handle), "Create Vulkan instance");
        try {
            // Load driver-owned resources before this instance's static cleanup is registered.
            uint32_t count{0};
            check(vkEnumeratePhysicalDevices(handle, &count, nullptr), "Enumerate Vulkan GPUs");
            devices.resize(count);
            check(vkEnumeratePhysicalDevices(handle, &count, devices.data()),
                  "Enumerate Vulkan GPUs");
            devices.resize(count);
        } catch (...) {
            vkDestroyInstance(handle, nullptr);
            throw;
        }
    }
    ~VulkanInstance() {
        vkDestroyInstance(handle, nullptr);
    }
    VulkanInstance(const VulkanInstance &) = delete;
    VulkanInstance &operator=(const VulkanInstance &) = delete;
};

std::shared_ptr<VulkanInstance> sharedInstance() {
    // Keep the ICD loaded across OpChain teardown and reconfiguration.
    static const auto instance{std::make_shared<VulkanInstance>()};
    return instance;
}

struct Parameters {
    std::array<uint32_t, 4> outputInfo{};
    std::array<uint32_t, 4> crop{};
    std::array<uint32_t, 4> inner{};
    std::array<uint32_t, 4> strides{};
    std::array<uint32_t, 4> offsets{};
    std::array<float, 4> mean{};
    std::array<float, 4> inverseStd{};
    std::array<float, 4> padding{};
    std::array<float, 4> yuvRange{};
    std::array<float, 4> yuvCoefficients{};
};

size_t planeCount(const RawImagePixelFormat format) {
    switch (format) {
    case RawImagePixelFormat::Bgra:
    case RawImagePixelFormat::Yuy2:
        return 1;
    case RawImagePixelFormat::Nv12:
        return 2;
    case RawImagePixelFormat::I420:
        return sourcePlaneCount;
    default:
        throw std::runtime_error("Vulkan preprocessing supports BGRA, NV12, I420 and YUY2");
    }
}

void validateFrame(const mediaio::VideoFrame &frame) {
    require(frame.width() && frame.height(), "Empty GPU input image");
    require(frame.planeCount() == planeCount(frame.format()), "Unexpected GPU input plane count");
    require(frame.format() != RawImagePixelFormat::Yuy2 || frame.width() % chromaSubsampling == 0,
            "YUY2 GPU input requires an even width");
    for (size_t index{0}; index < frame.planeCount(); ++index) {
        const auto &plane{frame.planes()[index]};
        const uint64_t width{index == 0 ? frame.width()
                                        : (uint64_t(frame.width()) + chromaSubsampling - 1) /
                                              chromaSubsampling};
        const uint64_t height{index == 0 ? frame.height()
                                         : (uint64_t(frame.height()) + chromaSubsampling - 1) /
                                               chromaSubsampling};
        const uint64_t pixelBytes{
            frame.format() == RawImagePixelFormat::Bgra ? bgraPixelBytes
            : frame.format() == RawImagePixelFormat::Yuy2 ||
                    (frame.format() == RawImagePixelFormat::Nv12 && index == 1)
                ? chromaSubsampling
                : 1};
        const uint64_t rowBytes{width * pixelBytes};
        const uint64_t extent{(height - 1) * plane.strideBytes() + rowBytes};
        require(plane.canRead() && plane.strideBytes() >= rowBytes && plane.byteSize() >= extent &&
                    plane.byteSize() <= std::numeric_limits<uint32_t>::max() - wordBytes,
                "GPU input plane has an invalid size or stride");
    }
}

std::array<uint32_t, 4> rectangle(const PixelRect &rect) {
    return {static_cast<uint32_t>(rect.x),
            static_cast<uint32_t>(rect.y),
            static_cast<uint32_t>(rect.width),
            static_cast<uint32_t>(rect.height)};
}
} // namespace

struct GpuPreprocess::State {
    std::shared_ptr<VulkanInstance> instance;
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    uint32_t queueFamily{0};
    VkDescriptorSetLayout descriptorLayout{VK_NULL_HANDLE};
    VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
    VkDescriptorSet descriptors{VK_NULL_HANDLE};
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    VkPipeline pipeline{VK_NULL_HANDLE};
    VkCommandPool commandPool{VK_NULL_HANDLE};
    VkCommandBuffer command{VK_NULL_HANDLE};
    VkFence completion{VK_NULL_HANDLE};
    std::array<std::unique_ptr<VulkanBuffer>, sourcePlaneCount> planes;
    std::unique_ptr<VulkanBuffer> tensor;
    std::unique_ptr<VulkanBuffer> readback;
    std::unique_ptr<VulkanBuffer> parameters;
    bool usable{true};
    size_t tensorBytes{0};
    std::string deviceIdentity;

    ~State() {
        if (device) {
            vkDeviceWaitIdle(device);
            planes = {};
            tensor.reset();
            readback.reset();
            parameters.reset();
            if (completion)
                vkDestroyFence(device, completion, nullptr);
            if (commandPool)
                vkDestroyCommandPool(device, commandPool, nullptr);
            if (pipeline)
                vkDestroyPipeline(device, pipeline, nullptr);
            if (pipelineLayout)
                vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            if (descriptorPool)
                vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            if (descriptorLayout)
                vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            vkDestroyDevice(device, nullptr);
        }
    }

    uint32_t memoryType(const uint32_t bits, const VkMemoryPropertyFlags required) const {
        for (uint32_t index{0}; index < memoryProperties.memoryTypeCount; ++index) {
            if ((bits & (uint32_t{1} << index)) &&
                (memoryProperties.memoryTypes[index].propertyFlags & required) == required)
                return index;
        }
        return memoryProperties.memoryTypeCount;
    }

    std::unique_ptr<VulkanBuffer> allocate(const VkDeviceSize bytes,
                                           const VkBufferUsageFlags usage,
                                           const bool hostVisible,
                                           const bool hostRead = false) const {
        auto buffer{std::make_unique<VulkanBuffer>(device, bytes)};
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = bytes;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device, &info, nullptr, &buffer->buffer), "Create GPU buffer");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, buffer->buffer, &requirements);
        const auto required{hostVisible ? VkMemoryPropertyFlags{VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT}
                                        : VkMemoryPropertyFlags{0}};
        const VkMemoryPropertyFlags cached{
            hostRead ? VkMemoryPropertyFlags{VK_MEMORY_PROPERTY_HOST_CACHED_BIT} : 0};
        const auto preferred{hostVisible
                                 ? required | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | cached
                                 : VkMemoryPropertyFlags{VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT}};
        uint32_t type{memoryType(requirements.memoryTypeBits, preferred)};
        if (type == memoryProperties.memoryTypeCount && hostRead)
            type = memoryType(requirements.memoryTypeBits, required | cached);
        if (type == memoryProperties.memoryTypeCount)
            type = memoryType(requirements.memoryTypeBits, required);
        if (type == memoryProperties.memoryTypeCount)
            return nullptr;
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        check(vkAllocateMemory(device, &allocation, nullptr, &buffer->memory),
              "Allocate GPU buffer");
        check(vkBindBufferMemory(device, buffer->buffer, buffer->memory, 0), "Bind GPU buffer");
        if (hostVisible) {
            check(vkMapMemory(device, buffer->memory, 0, VK_WHOLE_SIZE, 0, &buffer->mapped),
                  "Map GPU buffer");
            buffer->coherent = memoryProperties.memoryTypes[type].propertyFlags &
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        }
        return buffer;
    }

    void initialize(const size_t bytes) {
        tensorBytes = bytes;
        instance = sharedInstance();
        for (const auto candidate : instance->devices) {
            VkPhysicalDeviceProperties candidateProperties{};
            vkGetPhysicalDeviceProperties(candidate, &candidateProperties);
            if (candidateProperties.apiVersion < VK_API_VERSION_1_1 ||
                (candidateProperties.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU &&
                 candidateProperties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU))
                continue;
            uint32_t queueCount{0};
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(queueCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, families.data());
            for (uint32_t index{0}; index < queueCount; ++index) {
                if (families[index].queueCount &&
                    (families[index].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                    physicalDevice = candidate;
                    properties = candidateProperties;
                    queueFamily = index;
                    break;
                }
            }
            if (physicalDevice)
                break;
        }
        require(physicalDevice, "No hardware Vulkan 1.1 compute device is available");
        const auto &limits{properties.limits};
        require(bytes > 0 && bytes <= limits.maxStorageBufferRange &&
                    limits.maxPerStageDescriptorStorageBuffers >= storageBindingCount &&
                    limits.maxComputeWorkGroupInvocations >= workgroupSide * workgroupSide &&
                    limits.maxComputeWorkGroupSize[0] >= workgroupSide &&
                    limits.maxComputeWorkGroupSize[1] >= workgroupSide,
                "Vulkan device limits cannot support this tensor or preprocessing shader");
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
        deviceIdentity = fmt::format("vulkan/1/{}/{}/{}/{}/{}",
                                     properties.vendorID,
                                     properties.deviceID,
                                     properties.deviceName,
                                     properties.driverVersion,
                                     properties.apiVersion);
        const float priority{1.0F};
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = queueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        check(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device),
              "Create Vulkan device");
        vkGetDeviceQueue(device, queueFamily, 0, &queue);
        tensor = allocate(bytes,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          true,
                          true);
        if (!tensor) {
            tensor = allocate(bytes,
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              false);
            readback = allocate(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
            require(tensor && readback, "No compatible Vulkan tensor or readback memory");
        }
        parameters = allocate(sizeof(Parameters), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
        require(bool(parameters), "No host-visible Vulkan parameter memory");
        std::array<VkDescriptorSetLayoutBinding, descriptorBindingCount> bindings{};
        for (uint32_t index{0}; index < bindings.size(); ++index) {
            bindings[index].binding = index;
            bindings[index].descriptorType = index == parameterBinding
                                                 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                 : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[index].descriptorCount = 1;
            bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = bindings.size();
        layoutInfo.pBindings = bindings.data();
        check(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorLayout),
              "Create GPU bindings");
        const std::array poolSizes{
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, storageBindingCount},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = poolSizes.size();
        poolInfo.pPoolSizes = poolSizes.data();
        check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool),
              "Create GPU descriptor pool");
        VkDescriptorSetAllocateInfo setInfo{};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setInfo.descriptorPool = descriptorPool;
        setInfo.descriptorSetCount = 1;
        setInfo.pSetLayouts = &descriptorLayout;
        check(vkAllocateDescriptorSets(device, &setInfo, &descriptors), "Allocate GPU bindings");
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
        check(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout),
              "Create GPU pipeline layout");
        VkShaderModuleCreateInfo shaderInfo{};
        shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderInfo.codeSize = sizeof(preprocessingSpirv);
        shaderInfo.pCode = preprocessingSpirv;
        VkShaderModule shader{VK_NULL_HANDLE};
        check(vkCreateShaderModule(device, &shaderInfo, nullptr, &shader),
              "Load preprocessing shader");
        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = shader;
        pipelineInfo.stage.pName = "main";
        const auto pipelineResult{
            vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline)};
        vkDestroyShaderModule(device, shader, nullptr);
        check(pipelineResult, "Create preprocessing pipeline");
        VkCommandPoolCreateInfo commandPoolInfo{};
        commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        commandPoolInfo.queueFamilyIndex = queueFamily;
        commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        check(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool),
              "Create GPU command pool");
        VkCommandBufferAllocateInfo commandInfo{};
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandInfo.commandPool = commandPool;
        commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandInfo.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device, &commandInfo, &command), "Allocate GPU commands");
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        check(vkCreateFence(device, &fenceInfo, nullptr, &completion),
              "Create GPU completion fence");
    }
};

GpuPreprocess::GpuPreprocess(const size_t tensorBytes) : state(std::make_unique<State>()) {
    state->initialize(tensorBytes);
}
GpuPreprocess::~GpuPreprocess() = default;
std::string GpuPreprocess::identity() const {
    return state->deviceIdentity;
}
bool GpuPreprocess::tensorCopied() const {
    return bool(state->readback);
}

void GpuPreprocess::upload(const mediaio::VideoFrame &frame) {
    require(state->usable, "Vulkan preprocessing device is unavailable after a submission failure");
    validateFrame(frame);
    for (size_t index{0}; index < frame.planeCount(); ++index) {
        const auto &plane{frame.planes()[index]};
        require(plane.hasHostData(), "Vulkan upload requires readable host planes");
        const VkDeviceSize size{(plane.byteSize() + wordBytes - 1) / wordBytes * wordBytes};
        require(size <= state->properties.limits.maxStorageBufferRange,
                "Video exceeds Vulkan storage-buffer limits");
        auto &buffer{state->planes[index]};
        if (!buffer || buffer->size != size)
            buffer = state->allocate(size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        require(bool(buffer), "No host-visible Vulkan upload memory");
        std::memcpy(buffer->mapped, plane.data(), plane.byteSize());
        std::memset(
            static_cast<uint8_t *>(buffer->mapped) + plane.byteSize(), 0, size - plane.byteSize());
        buffer->flush();
    }
}

std::span<uint8_t> GpuPreprocess::run(const mediaio::VideoFrame &frame,
                                      const PixelRect &crop,
                                      const PixelRect &inner,
                                      const ModelInput &input) {
    require(state->usable, "Vulkan preprocessing device is unavailable after a submission failure");
    validateFrame(frame);
    size_t width{0}, height{0};
    require(input.tryGetImageTensorSize(width, height), "Invalid Vulkan model image shape");
    require(!crop.isEmpty() && crop.x < frame.width() && crop.y < frame.height() &&
                crop.width <= frame.width() - crop.x && crop.height <= frame.height() - crop.y,
            "GPU crop is outside the input frame");
    require(inner.width && inner.height && inner.x <= width && inner.y <= height &&
                inner.width <= width - inner.x && inner.height <= height - inner.y &&
                width <= std::numeric_limits<uint32_t>::max() / crop.width &&
                height <= std::numeric_limits<uint32_t>::max() / crop.height,
            "GPU image coordinates exceed shader arithmetic limits");
    const auto groupsX{(width + workgroupSide - 1) / workgroupSide};
    const auto groupsY{(height + workgroupSide - 1) / workgroupSide};
    require(groupsX <= state->properties.limits.maxComputeWorkGroupCount[0] &&
                groupsY <= state->properties.limits.maxComputeWorkGroupCount[1],
            "GPU output exceeds dispatch limits");
    SourceFormat format{SourceFormat::Bgra};
    switch (frame.format()) {
    case RawImagePixelFormat::Nv12:
        format = SourceFormat::Nv12;
        break;
    case RawImagePixelFormat::I420:
        format = SourceFormat::I420;
        break;
    case RawImagePixelFormat::Yuy2:
        format = SourceFormat::Yuy2;
        break;
    default:
        break;
    }
    Parameters values;
    values.outputInfo = {static_cast<uint32_t>(width),
                         static_cast<uint32_t>(height),
                         static_cast<uint32_t>(format),
                         input.dataKind == DataKind::ImageRgbChw};
    values.crop = rectangle(crop);
    values.inner = rectangle(inner);
    for (size_t index{0}; index < frame.planeCount(); ++index) {
        values.strides[index] = frame.planes()[index].strideBytes();
    }
    values.mean = {input.mean.r, input.mean.g, input.mean.b, 0};
    values.inverseStd = {1.0F / std::max(input.std.r, normalizationEpsilon),
                         1.0F / std::max(input.std.g, normalizationEpsilon),
                         1.0F / std::max(input.std.b, normalizationEpsilon),
                         0};
    values.padding = {std::clamp(input.letterboxRed, 0.0F, 1.0F),
                      std::clamp(input.letterboxGreen, 0.0F, 1.0F),
                      std::clamp(input.letterboxBlue, 0.0F, 1.0F),
                      0};
    if (format != SourceFormat::Bgra) {
        require(frame.yuvColorMatrix() != YuvColorMatrix::Unknown &&
                    frame.yuvRange() != YuvRange::Unknown,
                "GPU preprocessing needs known YUV colorimetry and range");
        const auto coefficients{getYuvToRgbCoefficients(frame.yuvColorMatrix())};
        const float greenWeight{1.0F - coefficients.kr - coefficients.kb};
        const float red{chromaCoefficientScale * (1.0F - coefficients.kr)};
        const float blue{chromaCoefficientScale * (1.0F - coefficients.kb)};
        values.yuvCoefficients = {
            red, blue, coefficients.kb * blue / greenWeight, coefficients.kr * red / greenWeight};
        const bool limited{frame.yuvRange() == YuvRange::Limited};
        values.yuvRange = {limited ? limitedLumaOffset : 0.0F,
                           1.0F / (limited ? limitedLumaRange : byteMaximum),
                           chromaCenter,
                           1.0F / (limited ? limitedChromaRange : byteMaximum)};
    }
    std::memcpy(state->parameters->mapped, &values, sizeof(values));
    state->parameters->flush();
    std::array<VkDescriptorBufferInfo, descriptorBindingCount> infos{};
    std::array<VkWriteDescriptorSet, descriptorBindingCount> writes{};
    for (uint32_t index{0}; index < infos.size(); ++index) {
        const auto *buffer{index == tensorBinding ? state->tensor.get()
                           : index == parameterBinding
                               ? state->parameters.get()
                               : state->planes[index < frame.planeCount() ? index : 0].get()};
        require(buffer, "GPU input was not prepared");
        infos[index] = {buffer->buffer, 0, buffer->size};
        writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[index].dstSet = state->descriptors;
        writes[index].dstBinding = index;
        writes[index].descriptorCount = 1;
        writes[index].descriptorType = index == parameterBinding
                                           ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                           : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[index].pBufferInfo = &infos[index];
    }
    vkUpdateDescriptorSets(state->device, writes.size(), writes.data(), 0, nullptr);
    check(vkResetCommandBuffer(state->command, 0), "Reset GPU commands");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(state->command, &begin), "Begin GPU commands");
    vkCmdBindPipeline(state->command, VK_PIPELINE_BIND_POINT_COMPUTE, state->pipeline);
    vkCmdBindDescriptorSets(state->command,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            state->pipelineLayout,
                            0,
                            1,
                            &state->descriptors,
                            0,
                            nullptr);
    vkCmdDispatch(
        state->command, static_cast<uint32_t>(groupsX), static_cast<uint32_t>(groupsY), 1);
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = state->readback ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(state->command,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         state->readback ? VK_PIPELINE_STAGE_TRANSFER_BIT
                                         : VK_PIPELINE_STAGE_HOST_BIT,
                         0,
                         1,
                         &barrier,
                         0,
                         nullptr,
                         0,
                         nullptr);
    if (state->readback) {
        const VkBufferCopy copy{0, 0, state->tensorBytes};
        vkCmdCopyBuffer(state->command, state->tensor->buffer, state->readback->buffer, 1, &copy);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(state->command,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT,
                             0,
                             1,
                             &barrier,
                             0,
                             nullptr,
                             0,
                             nullptr);
    }
    check(vkEndCommandBuffer(state->command), "End GPU commands");
    check(vkResetFences(state->device, 1, &state->completion), "Reset GPU completion fence");
    VkSubmitInfo submission{};
    submission.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submission.commandBufferCount = 1;
    submission.pCommandBuffers = &state->command;
    try {
        check(vkQueueSubmit(state->queue, 1, &submission, state->completion),
              "Submit GPU preprocessing");
        check(vkWaitForFences(state->device, 1, &state->completion, VK_TRUE, gpuWaitNanoseconds),
              "Wait for GPU preprocessing");
    } catch (...) {
        // A timeout cannot release camera ownership while the GPU might still read it.
        vkDeviceWaitIdle(state->device);
        state->usable = false;
        state->planes = {};
        throw;
    }
    auto *const output{state->readback ? state->readback.get() : state->tensor.get()};
    output->invalidate();
    return {static_cast<uint8_t *>(output->mapped), state->tensorBytes};
}
} // namespace opk::vulkan
