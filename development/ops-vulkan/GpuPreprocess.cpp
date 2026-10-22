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
#include "preprocess_push_spirv.h"
#include "preprocess_spirv.h"
#ifdef __linux__
#include "LinuxImport.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace opk::vulkan {
namespace {
// Specialization IDs, buffer slots and word size must match preprocess.comp.
constexpr uint32_t workgroupWidthSpecializationId{0};
constexpr uint32_t workgroupHeightSpecializationId{1};
constexpr uint32_t fallbackWorkgroupSidePixels{8};
constexpr uint32_t optimizedWorkgroupSidePixels{16};
constexpr size_t maxSourcePlaneCount{3};
constexpr size_t storageBufferBindingCount{maxSourcePlaneCount + 1};
constexpr size_t shaderBindingCount{storageBufferBindingCount + 1};
constexpr uint32_t outputTensorBinding{maxSourcePlaneCount};
constexpr uint32_t preprocessParametersBinding{storageBufferBindingCount};
constexpr VkDeviceSize shaderWordBytes{sizeof(uint32_t)};

// Pixel layout.
constexpr uint32_t chromaSubsamplingFactor{2};
constexpr uint32_t bgraBytesPerPixel{4};
constexpr uint32_t yuy2BytesPerPixel{2};
constexpr uint32_t nv12BytesPerChromaSample{2};

// 8-bit color conversion: limited luma spans 16..235, chroma 16..240 with neutral 128.
constexpr float max8BitComponentValue{255.0F};
constexpr float limitedLumaOffset{16.0F};
constexpr float limitedLumaRange{219.0F};
constexpr float chromaCenter{128.0F};
constexpr float limitedChromaRange{224.0F};
constexpr float chromaCoefficientScale{2.0F};

// Numerical and execution limits.
constexpr float minimumStandardDeviation{1e-12F}; // Used to protect against division by zero
constexpr uint64_t gpuCompletionTimeoutNanoseconds{5'000'000'000};

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

// Mirrors the shader's uvec4/vec4 offsets in both std140 and std430.
// Each member occupies 16 bytes. Three-value fields reserve their fourth component as padding.
// https://docs.vulkan.org/guide/latest/shader_memory_layout.html#alignment-requirements
struct PreprocessingShaderParameters {
    // Output width/height in pixels, SourceFormat value, tensor layout (1 = CHW, 0 = HWC).
    std::array<uint32_t, 4> outputInfo{};

    // Rectangles: x, y, width, height in pixels.
    std::array<uint32_t, 4> crop{};  // Region in the source image.
    std::array<uint32_t, 4> inner{}; // Resized image area in the output, excluding letterbox bars.

    // Plane 0, plane 1, plane 2, padding; values are in bytes. Absent planes are unused.
    std::array<uint32_t, 4> strides{}; // Distance between row starts.
    std::array<uint32_t, 4> offsets{}; // Plane starts within their bound Vulkan buffers.

    // Red, green, blue, padding.
    std::array<float, 4> mean{};       // Model normalization means.
    std::array<float, 4> inverseStd{}; // Reciprocals of the guarded model standard deviations.
    std::array<float, 4> padding{};    // Letterbox color in [0, 1], before normalization.

    // Luma offset, inverse luma range, chroma center, inverse chroma range.
    std::array<float, 4> yuvRange{};
    // Cr-to-red, Cb-to-blue, Cb-to-green, Cr-to-green gains; subtract the green terms.
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
        return maxSourcePlaneCount;
    default:
        throw std::runtime_error("Vulkan preprocessing supports BGRA, NV12, I420 and YUY2");
    }
}

std::array<size_t, maxSourcePlaneCount> validateFrame(const mediaio::VideoFrame &frame) {
    require(frame.width() && frame.height(), "Empty GPU input image");
    require(frame.planeCount() == planeCount(frame.format()), "Unexpected GPU input plane count");
    require(frame.format() != RawImagePixelFormat::Yuy2 ||
                frame.width() % chromaSubsamplingFactor == 0,
            "YUY2 GPU input requires an even width");
    std::array<size_t, maxSourcePlaneCount> planeSizes{};
    for (size_t index{0}; index < frame.planeCount(); ++index) {
        const auto &plane{frame.planes()[index]};
        const uint64_t width{
            index == 0 ? frame.width()
                       : (static_cast<uint64_t>(frame.width()) + chromaSubsamplingFactor - 1) /
                             chromaSubsamplingFactor};
        const uint64_t height{
            index == 0 ? frame.height()
                       : (static_cast<uint64_t>(frame.height()) + chromaSubsamplingFactor - 1) /
                             chromaSubsamplingFactor};
        const uint64_t pixelBytes{frame.format() == RawImagePixelFormat::Bgra   ? bgraBytesPerPixel
                                  : frame.format() == RawImagePixelFormat::Yuy2 ? yuy2BytesPerPixel
                                  : frame.format() == RawImagePixelFormat::Nv12 && index == 1
                                      ? nv12BytesPerChromaSample
                                      : 1};
        const uint64_t rowBytes{width * pixelBytes};
        const uint64_t extent{(height - 1) * plane.strideBytes() + rowBytes};
        require(plane.canRead() && plane.strideBytes() >= rowBytes && plane.byteSize() >= extent &&
                    plane.byteSize() <= std::numeric_limits<uint32_t>::max() - shaderWordBytes,
                "GPU input plane has an invalid size or stride");
        planeSizes[index] = extent;
    }
    return planeSizes;
}

std::array<uint32_t, 4> rectangle(const PixelRect &rect) {
    return {static_cast<uint32_t>(rect.x),
            static_cast<uint32_t>(rect.y),
            static_cast<uint32_t>(rect.width),
            static_cast<uint32_t>(rect.height)};
}
SourceFormat shaderSourceFormat(const RawImagePixelFormat pixelFormat) {
    switch (pixelFormat) {
    case RawImagePixelFormat::Nv12:
        return SourceFormat::Nv12;
    case RawImagePixelFormat::I420:
        return SourceFormat::I420;
    case RawImagePixelFormat::Yuy2:
        return SourceFormat::Yuy2;
    default:
        return SourceFormat::Bgra;
    }
}

std::array<float, 4> shaderYuvCoefficients(const YuvColorMatrix matrix) {
    const auto coefficients{getYuvToRgbCoefficients(matrix)};
    const float greenWeight{1.0F - coefficients.kr - coefficients.kb};
    const float red{chromaCoefficientScale * (1.0F - coefficients.kr)};
    const float blue{chromaCoefficientScale * (1.0F - coefficients.kb)};
    return {red, blue, coefficients.kb * blue / greenWeight, coefficients.kr * red / greenWeight};
}

std::array<float, 4> shaderYuvRange(const YuvRange range) {
    const bool limited{range == YuvRange::Limited};
    return {limited ? limitedLumaOffset : 0.0F,
            1.0F / (limited ? limitedLumaRange : max8BitComponentValue),
            chromaCenter,
            1.0F / (limited ? limitedChromaRange : max8BitComponentValue)};
}

} // namespace

struct GpuPreprocess::State {
    // Device identity, capabilities and compute queue.
    std::shared_ptr<VulkanInstance> instance;
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    uint32_t queueFamily{0};
    std::string deviceIdentity;
    bool dmaImportSupported{false};
    bool usePushConstants{false};
    uint32_t workgroupSidePixels{fallbackWorkgroupSidePixels};

    // Shader pipeline, buffer bindings and parameter storage.
    VkDescriptorSetLayout descriptorLayout{VK_NULL_HANDLE};
    VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
    VkDescriptorSet descriptors{VK_NULL_HANDLE};
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    VkPipeline pipeline{VK_NULL_HANDLE};
    std::unique_ptr<VulkanBuffer> parameters;

    // Command recording and completion tracking.
    VkCommandPool commandPool{VK_NULL_HANDLE};
    VkCommandBuffer command{VK_NULL_HANDLE};
    VkFence completion{VK_NULL_HANDLE};

    // Source image planes, uploaded or imported from DMA-BUFs.
    std::array<std::unique_ptr<VulkanBuffer>, maxSourcePlaneCount> planes;
    std::array<uint32_t, maxSourcePlaneCount> offsets{};
    bool imported{false};

    // Output tensor and optional CPU-readable copy.
    std::unique_ptr<VulkanBuffer> tensor;
    std::unique_ptr<VulkanBuffer> readback;
    size_t tensorBytes{0};

    // Indicate the usability of this state.
    bool usable{true};

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

    uint32_t findMatchingMemoryType(const uint32_t bits,
                                    const VkMemoryPropertyFlags required) const {
        for (uint32_t index{0}; index < memoryProperties.memoryTypeCount; ++index) {
            if ((bits & (uint32_t{1} << index)) &&
                (memoryProperties.memoryTypes[index].propertyFlags & required) == required)
                return index;
        }
        return memoryProperties.memoryTypeCount;
    }

    std::unique_ptr<VulkanBuffer> createBufferWithoutMemory(const VkDeviceSize bytes,
                                                            const VkBufferUsageFlags usage) const {
        auto buffer{std::make_unique<VulkanBuffer>(device, bytes)};
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = bytes;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device, &info, nullptr, &buffer->buffer), "Create GPU buffer");
        return buffer;
    }

    uint32_t findPreferredMemoryType(const uint32_t compatibleTypes,
                                     const bool hostVisible,
                                     const bool hostRead) const {
        const auto required{hostVisible ? VkMemoryPropertyFlags{VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT}
                                        : VkMemoryPropertyFlags{0}};
        const VkMemoryPropertyFlags cached{
            hostRead ? VkMemoryPropertyFlags{VK_MEMORY_PROPERTY_HOST_CACHED_BIT} : 0};
        const auto preferred{hostVisible
                                 ? required | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | cached
                                 : VkMemoryPropertyFlags{VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT}};
        uint32_t type{findMatchingMemoryType(compatibleTypes, preferred)};
        if (type == memoryProperties.memoryTypeCount && hostRead)
            type = findMatchingMemoryType(compatibleTypes, required | cached);
        if (type == memoryProperties.memoryTypeCount)
            type = findMatchingMemoryType(compatibleTypes, required);
        return type;
    }

    VkDeviceMemory allocateMemory(const VkMemoryRequirements &requirements,
                                  const uint32_t memoryTypeIndex) const {
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryTypeIndex;
        VkDeviceMemory memory{VK_NULL_HANDLE};
        check(vkAllocateMemory(device, &allocation, nullptr, &memory), "Allocate GPU buffer");
        return memory;
    }

    uint8_t *mapMemory(const VkDeviceMemory memory) const {
        void *mapped{nullptr};
        check(vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped), "Map GPU buffer");
        return static_cast<uint8_t *>(mapped);
    }

    std::unique_ptr<VulkanBuffer> createBufferWithMemory(const VkDeviceSize bytes,
                                                         const VkBufferUsageFlags usage,
                                                         const bool hostVisible,
                                                         const bool hostRead = false) const {
        auto buffer{createBufferWithoutMemory(bytes, usage)};
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, buffer->buffer, &requirements);
        const auto memoryTypeIndex{
            findPreferredMemoryType(requirements.memoryTypeBits, hostVisible, hostRead)};
        if (memoryTypeIndex == memoryProperties.memoryTypeCount)
            return nullptr;
        buffer->memory = allocateMemory(requirements, memoryTypeIndex);
        check(vkBindBufferMemory(device, buffer->buffer, buffer->memory, 0), "Bind GPU buffer");
        if (hostVisible) {
            buffer->mapped = mapMemory(buffer->memory);
            buffer->coherent = memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags &
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        }
        return buffer;
    }

    void selectPhysicalDevice() {
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
    }

    void validateDeviceLimits() {
        const auto &limits{properties.limits};
        usePushConstants = limits.maxPushConstantsSize >= sizeof(PreprocessingShaderParameters);
        const bool supportsOptimizedWorkgroup{
            limits.maxComputeWorkGroupInvocations >=
                optimizedWorkgroupSidePixels * optimizedWorkgroupSidePixels &&
            limits.maxComputeWorkGroupSize[0] >= optimizedWorkgroupSidePixels &&
            limits.maxComputeWorkGroupSize[1] >= optimizedWorkgroupSidePixels};
        workgroupSidePixels =
            supportsOptimizedWorkgroup ? optimizedWorkgroupSidePixels : fallbackWorkgroupSidePixels;
        require(tensorBytes > 0 && tensorBytes <= limits.maxStorageBufferRange &&
                    limits.maxPerStageDescriptorStorageBuffers >= storageBufferBindingCount &&
                    limits.maxComputeWorkGroupInvocations >=
                        workgroupSidePixels * workgroupSidePixels &&
                    limits.maxComputeWorkGroupSize[0] >= workgroupSidePixels &&
                    limits.maxComputeWorkGroupSize[1] >= workgroupSidePixels,
                "Vulkan device limits cannot support this tensor or preprocessing shader");
    }

    std::vector<const char *> supportedDmaBufExtensions() const {
        uint32_t extensionCount{0};
        check(
            vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr),
            "Enumerate Vulkan extensions");
        std::vector<VkExtensionProperties> extensions(extensionCount);
        check(vkEnumerateDeviceExtensionProperties(
                  physicalDevice, nullptr, &extensionCount, extensions.data()),
              "Enumerate Vulkan extensions");
        std::vector<const char *> enabled;
#ifdef __linux__
        const std::array importExtensions{VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                                          VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
                                          VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME};
        const bool supported{std::ranges::all_of(importExtensions, [&](const char *const name) {
            return std::ranges::any_of(extensions, [&](const auto &extension) {
                return std::strcmp(name, extension.extensionName) == 0;
            });
        })};
        if (supported)
            enabled.assign(importExtensions.begin(), importExtensions.end());
#endif
        return enabled;
    }

    void createDevice() {
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
        deviceIdentity = fmt::format("vulkan/1/{}/{}/{}/{}/{}",
                                     properties.vendorID,
                                     properties.deviceID,
                                     properties.deviceName,
                                     properties.driverVersion,
                                     properties.apiVersion);
        const auto enabled{supportedDmaBufExtensions()};
        dmaImportSupported = !enabled.empty();
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
        deviceInfo.enabledExtensionCount = static_cast<uint32_t>(enabled.size());
        deviceInfo.ppEnabledExtensionNames = enabled.data();
        check(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device),
              "Create Vulkan device");
        vkGetDeviceQueue(device, queueFamily, 0, &queue);
    }

    void allocateWorkingBuffers() {
        tensor = createBufferWithMemory(tensorBytes,
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                        true,
                                        true);
        if (!tensor) {
            tensor = createBufferWithMemory(tensorBytes,
                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                            false);
            readback =
                createBufferWithMemory(tensorBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
            require(tensor && readback, "No compatible Vulkan tensor or readback memory");
        }
        if (!usePushConstants) {
            parameters = createBufferWithMemory(
                sizeof(PreprocessingShaderParameters), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
            require(parameters != nullptr, "No host-visible Vulkan parameter memory");
        }
    }

    void createBufferBindings() {
        std::array<VkDescriptorSetLayoutBinding, shaderBindingCount> bindings{};
        const auto bindingCount{usePushConstants ? storageBufferBindingCount : shaderBindingCount};
        for (uint32_t index{0}; index < bindingCount; ++index) {
            bindings[index].binding = index;
            bindings[index].descriptorType = index == preprocessParametersBinding
                                                 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                 : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[index].descriptorCount = 1;
            bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = bindingCount;
        layoutInfo.pBindings = bindings.data();
        check(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorLayout),
              "Create GPU bindings");
        const std::array poolSizes{
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, storageBufferBindingCount},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = usePushConstants ? poolSizes.size() - 1 : poolSizes.size();
        poolInfo.pPoolSizes = poolSizes.data();
        check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool),
              "Create GPU descriptor pool");
        VkDescriptorSetAllocateInfo setInfo{};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setInfo.descriptorPool = descriptorPool;
        setInfo.descriptorSetCount = 1;
        setInfo.pSetLayouts = &descriptorLayout;
        check(vkAllocateDescriptorSets(device, &setInfo, &descriptors), "Allocate GPU bindings");
    }

    void createComputePipeline() {
        const VkPushConstantRange pushRange{
            VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PreprocessingShaderParameters)};
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
        if (usePushConstants) {
            pipelineLayoutInfo.pushConstantRangeCount = 1;
            pipelineLayoutInfo.pPushConstantRanges = &pushRange;
        }
        check(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout),
              "Create GPU pipeline layout");
        VkShaderModuleCreateInfo shaderInfo{};
        shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderInfo.codeSize =
            usePushConstants ? sizeof(preprocessingPushSpirv) : sizeof(preprocessingSpirv);
        shaderInfo.pCode = usePushConstants ? preprocessingPushSpirv : preprocessingSpirv;
        VkShaderModule shader{VK_NULL_HANDLE};
        check(vkCreateShaderModule(device, &shaderInfo, nullptr, &shader),
              "Load preprocessing shader");
        const std::array workgroupEntries{
            VkSpecializationMapEntry{
                workgroupWidthSpecializationId, 0, sizeof(workgroupSidePixels)},
            VkSpecializationMapEntry{
                workgroupHeightSpecializationId, 0, sizeof(workgroupSidePixels)}};
        const VkSpecializationInfo workgroupSpecialization{workgroupEntries.size(),
                                                           workgroupEntries.data(),
                                                           sizeof(workgroupSidePixels),
                                                           &workgroupSidePixels};
        const VkComputePipelineCreateInfo pipelineInfo{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                      .pNext = nullptr,
                      .flags = 0,
                      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                      .module = shader,
                      .pName = "main",
                      .pSpecializationInfo = &workgroupSpecialization},
            .layout = pipelineLayout,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0};
        const auto pipelineResult{
            vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline)};
        vkDestroyShaderModule(device, shader, nullptr);
        check(pipelineResult, "Create preprocessing pipeline");
    }

    void createCommandResources() {
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

    void initialize(const size_t bytes) {
        tensorBytes = bytes;
        instance = sharedInstance();
        selectPhysicalDevice();
        validateDeviceLimits();
        createDevice();
        allocateWorkingBuffers();
        createBufferBindings();
        createComputePipeline();
        createCommandResources();
    }

    std::array<size_t, 2> validateShaderInput(const mediaio::VideoFrame &frame,
                                              const PixelRect &crop,
                                              const PixelRect &inner,
                                              const ModelInput &input) const {
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
        const auto groupsX{(width + workgroupSidePixels - 1) / workgroupSidePixels};
        const auto groupsY{(height + workgroupSidePixels - 1) / workgroupSidePixels};
        require(groupsX <= properties.limits.maxComputeWorkGroupCount[0] &&
                    groupsY <= properties.limits.maxComputeWorkGroupCount[1],
                "GPU output exceeds dispatch limits");
        if (frame.format() != RawImagePixelFormat::Bgra)
            require(frame.yuvColorMatrix() != YuvColorMatrix::Unknown &&
                        frame.yuvRange() != YuvRange::Unknown,
                    "GPU preprocessing needs known YUV colorimetry and range");
        return {width, height};
    }

    PreprocessingShaderParameters createShaderParameters(const mediaio::VideoFrame &frame,
                                                         const PixelRect &crop,
                                                         const PixelRect &inner,
                                                         const ModelInput &input) const {
        const auto [width, height]{validateShaderInput(frame, crop, inner, input)};
        const auto format{shaderSourceFormat(frame.format())};
        PreprocessingShaderParameters values{};
        values.outputInfo = {static_cast<uint32_t>(width),
                             static_cast<uint32_t>(height),
                             static_cast<uint32_t>(format),
                             input.dataKind == DataKind::ImageRgbChw};
        values.crop = rectangle(crop);
        values.inner = rectangle(inner);
        for (size_t index{0}; index < frame.planeCount(); ++index) {
            values.strides[index] = frame.planes()[index].strideBytes();
            values.offsets[index] = offsets[index];
        }
        values.mean = {input.mean.r, input.mean.g, input.mean.b, 0};
        values.inverseStd = {1.0F / std::max(input.std.r, minimumStandardDeviation),
                             1.0F / std::max(input.std.g, minimumStandardDeviation),
                             1.0F / std::max(input.std.b, minimumStandardDeviation),
                             0};
        values.padding = {std::clamp(input.letterboxRed, 0.0F, 1.0F),
                          std::clamp(input.letterboxGreen, 0.0F, 1.0F),
                          std::clamp(input.letterboxBlue, 0.0F, 1.0F),
                          0};
        if (frame.format() != RawImagePixelFormat::Bgra) {
            values.yuvCoefficients = shaderYuvCoefficients(frame.yuvColorMatrix());
            values.yuvRange = shaderYuvRange(frame.yuvRange());
        }
        return values;
    }

    void writeShaderParameters(const PreprocessingShaderParameters &values) const {
        if (parameters) {
            std::memcpy(parameters->mapped, &values, sizeof(values));
            parameters->flush();
        }
    }

    void updateBufferBindings(const size_t sourcePlaneCount) const {
        std::array<VkDescriptorBufferInfo, shaderBindingCount> infos{};
        std::array<VkWriteDescriptorSet, shaderBindingCount> writes{};
        const auto bindingCount{usePushConstants ? storageBufferBindingCount : shaderBindingCount};
        for (uint32_t index{0}; index < bindingCount; ++index) {
            const auto *const buffer{index == outputTensorBinding ? tensor.get()
                                     : index == preprocessParametersBinding
                                         ? parameters.get()
                                         : planes[index < sourcePlaneCount ? index : 0].get()};
            require(buffer, "GPU input was not prepared");
            infos[index] = {buffer->buffer, 0, buffer->size};
            writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[index].dstSet = descriptors;
            writes[index].dstBinding = index;
            writes[index].descriptorCount = 1;
            writes[index].descriptorType = index == preprocessParametersBinding
                                               ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                               : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[index].pBufferInfo = &infos[index];
        }
        vkUpdateDescriptorSets(device, bindingCount, writes.data(), 0, nullptr);
    }

    void recordTensorReadback() const {
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = readback ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             readback ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_HOST_BIT,
                             0,
                             1,
                             &barrier,
                             0,
                             nullptr,
                             0,
                             nullptr);
        if (readback) {
            const VkBufferCopy copy{0, 0, tensorBytes};
            vkCmdCopyBuffer(command, tensor->buffer, readback->buffer, 1, &copy);
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(command,
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
    }

    void recordPreprocessingCommands(const PreprocessingShaderParameters &values) const {
        const auto width{values.outputInfo[0]};
        const auto height{values.outputInfo[1]};
        const auto groupsX{(width + workgroupSidePixels - 1) / workgroupSidePixels};
        const auto groupsY{(height + workgroupSidePixels - 1) / workgroupSidePixels};
        check(vkResetCommandBuffer(command, 0), "Reset GPU commands");
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command, &begin), "Begin GPU commands");
        externalBarrier(true);
        if (usePushConstants)
            vkCmdPushConstants(
                command, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(values), &values);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(command,
                                VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipelineLayout,
                                0,
                                1,
                                &descriptors,
                                0,
                                nullptr);
        vkCmdDispatch(command, static_cast<uint32_t>(groupsX), static_cast<uint32_t>(groupsY), 1);
        externalBarrier(false);
        recordTensorReadback();
        check(vkEndCommandBuffer(command), "End GPU commands");
    }

    void submitAndWait() {
        check(vkResetFences(device, 1, &completion), "Reset GPU completion fence");
        VkSubmitInfo submission{};
        submission.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submission.commandBufferCount = 1;
        submission.pCommandBuffers = &command;
        try {
            check(vkQueueSubmit(queue, 1, &submission, completion), "Submit GPU preprocessing");
            check(vkWaitForFences(device, 1, &completion, VK_TRUE, gpuCompletionTimeoutNanoseconds),
                  "Wait for GPU preprocessing");
        } catch (...) {
            // A timeout cannot release camera ownership while the GPU might still read it.
            vkDeviceWaitIdle(device);
            usable = false;
            planes = {};
            throw;
        }
    }

    std::span<uint8_t> readableTensor() {
        const auto *const output{readback ? readback.get() : tensor.get()};
        output->invalidate();
        if (imported)
            planes = {};
        return {output->mapped, tensorBytes};
    }

    void externalBarrier(const bool acquire) const {
        if (!imported)
            return;
        std::vector<VkBufferMemoryBarrier> barriers;
        for (const auto &plane : planes) {
            if (!plane)
                continue;
            VkBufferMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier.srcQueueFamilyIndex = acquire ? VK_QUEUE_FAMILY_FOREIGN_EXT : queueFamily;
            barrier.dstQueueFamilyIndex = acquire ? queueFamily : VK_QUEUE_FAMILY_FOREIGN_EXT;
            barrier.srcAccessMask = acquire ? 0 : VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = acquire ? VK_ACCESS_SHADER_READ_BIT : 0;
            barrier.buffer = plane->buffer;
            barrier.size = VK_WHOLE_SIZE;
            barriers.push_back(barrier);
        }
        vkCmdPipelineBarrier(
            command,
            acquire ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            acquire ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0,
            nullptr,
            static_cast<uint32_t>(barriers.size()),
            barriers.data(),
            0,
            nullptr);
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
    return state->readback != nullptr;
}

bool GpuPreprocess::import(const mediaio::VideoFrame &frame) {
    require(state->usable, "Vulkan preprocessing device is unavailable after a submission failure");
    if (!state->dmaImportSupported || frame.memoryType() != MemoryType::DmaBuf ||
        frame.planes().empty())
        return false;
    validateFrame(frame);
#ifdef __linux__
    std::array<std::unique_ptr<VulkanBuffer>, maxSourcePlaneCount> importedPlanes;
    for (size_t index{0}; index < frame.planeCount(); ++index) {
        importedPlanes[index] = importDmaBuf(state->physicalDevice,
                                             state->device,
                                             frame.planes()[index],
                                             state->properties.limits.maxStorageBufferRange);
        if (!importedPlanes[index])
            return false;
    }
    state->planes = std::move(importedPlanes);
    state->offsets = {};
    for (size_t index{0}; index < frame.planeCount(); ++index)
        state->offsets[index] = static_cast<uint32_t>(frame.planes()[index].offset());
    state->imported = true;
    return true;
#else
    return false;
#endif
}

void GpuPreprocess::upload(const mediaio::VideoFrame &frame) {
    require(state->usable, "Vulkan preprocessing device is unavailable after a submission failure");
    const auto planeSizes{validateFrame(frame)};
    if (state->imported)
        state->planes = {};
    state->imported = false;
    state->offsets = {};
    for (size_t index{0}; index < frame.planeCount(); ++index) {
        const auto &plane{frame.planes()[index]};
        require(plane.hasHostData(), "Vulkan upload requires readable host planes");
        // The final row need not include stride padding in the mapped source.
        const auto sourceBytes{planeSizes[index]};
        const VkDeviceSize size{(sourceBytes + shaderWordBytes - 1) / shaderWordBytes *
                                shaderWordBytes};
        require(size <= state->properties.limits.maxStorageBufferRange,
                "Video exceeds Vulkan storage-buffer limits");
        auto &buffer{state->planes[index]};
        if (!buffer || buffer->size != size)
            buffer = state->createBufferWithMemory(size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        require(buffer != nullptr, "No host-visible Vulkan upload memory");
        std::memcpy(buffer->mapped, plane.data(), sourceBytes);
        std::memset(buffer->mapped + sourceBytes, 0, size - sourceBytes);
        buffer->flush();
    }
}

std::span<uint8_t> GpuPreprocess::run(const mediaio::VideoFrame &frame,
                                      const PixelRect &crop,
                                      const PixelRect &inner,
                                      const ModelInput &input) {
    require(state->usable, "Vulkan preprocessing device is unavailable after a submission failure");
    const auto values{state->createShaderParameters(frame, crop, inner, input)};
    state->writeShaderParameters(values);
    state->updateBufferBindings(frame.planeCount());
    state->recordPreprocessingCommands(values);
    state->submitAndWait();
    return state->readableTensor();
}
} // namespace opk::vulkan
