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

#include <mlworkloadlib/context.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace opk::mlwl {

// MLWL's default context also requires compute, which graph-only devices need not support.
class GraphContext {
  public:
    GraphContext() {
        const vk::ApplicationInfo application("opk-mlwl", 1, nullptr, 0, VK_API_VERSION_1_4);
        instance = vk::raii::Instance(loader, vk::InstanceCreateInfo({}, &application));
        std::string lastFailure;
        for (const auto &candidate : vk::raii::PhysicalDevices(instance)) {
            const auto apiVersion = candidate.getProperties().apiVersion;
            if (apiVersion < VK_API_VERSION_1_3)
                continue;
            const auto available = candidate.enumerateDeviceExtensionProperties();
            const auto hasExtension = [&](const char *name) {
                return std::any_of(available.begin(), available.end(), [&](const auto &extension) {
                    return std::string_view(extension.extensionName.data()) == name;
                });
            };
            std::vector<const char *> extensions = {VK_ARM_DATA_GRAPH_EXTENSION_NAME,
                                                    VK_ARM_TENSORS_EXTENSION_NAME,
                                                    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
            if (!std::all_of(extensions.begin(), extensions.end(), hasExtension))
                continue;
            // maintenance5 is core in Vulkan 1.4; older devices need the extension name.
            if (hasExtension(VK_KHR_MAINTENANCE_5_EXTENSION_NAME))
                extensions.push_back(VK_KHR_MAINTENANCE_5_EXTENSION_NAME);
            else if (apiVersion < VK_API_VERSION_1_4)
                continue;

            const auto supported = candidate.getFeatures2<vk::PhysicalDeviceFeatures2,
                                                          vk::PhysicalDeviceTensorFeaturesARM,
                                                          vk::PhysicalDeviceDataGraphFeaturesARM,
                                                          vk::PhysicalDeviceVulkan12Features,
                                                          vk::PhysicalDeviceVulkan13Features>();
            if (!supported.get<vk::PhysicalDeviceTensorFeaturesARM>().tensors ||
                !supported.get<vk::PhysicalDeviceTensorFeaturesARM>().shaderTensorAccess ||
                !supported.get<vk::PhysicalDeviceDataGraphFeaturesARM>().dataGraph ||
                !supported.get<vk::PhysicalDeviceDataGraphFeaturesARM>().dataGraphShaderModule ||
                !supported.get<vk::PhysicalDeviceVulkan12Features>().shaderInt8 ||
                !supported.get<vk::PhysicalDeviceVulkan12Features>().vulkanMemoryModel ||
                !supported.get<vk::PhysicalDeviceVulkan13Features>().synchronization2)
                continue;

            vk::StructureChain<vk::PhysicalDeviceFeatures2,
                               vk::PhysicalDeviceTensorFeaturesARM,
                               vk::PhysicalDeviceDataGraphFeaturesARM,
                               vk::PhysicalDeviceVulkan12Features,
                               vk::PhysicalDeviceVulkan13Features>
                enabled;
            enabled.get<vk::PhysicalDeviceTensorFeaturesARM>().tensors = true;
            enabled.get<vk::PhysicalDeviceTensorFeaturesARM>().shaderTensorAccess = true;
            auto &graph = enabled.get<vk::PhysicalDeviceDataGraphFeaturesARM>();
            graph.dataGraph = true;
            graph.dataGraphShaderModule = true;
            graph.dataGraphSpecializationConstants =
                supported.get<vk::PhysicalDeviceDataGraphFeaturesARM>()
                    .dataGraphSpecializationConstants;
            enabled.get<vk::PhysicalDeviceVulkan12Features>().shaderInt8 = true;
            enabled.get<vk::PhysicalDeviceVulkan12Features>().vulkanMemoryModel = true;
            enabled.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 = true;

            const auto families = candidate.getQueueFamilyProperties();
            for (uint32_t i = 0; i < families.size(); ++i) {
                if (!families[i].queueCount ||
                    !(families[i].queueFlags & vk::QueueFlagBits::eDataGraphARM))
                    continue;
                const float priority = 1.0f;
                const vk::DeviceQueueCreateInfo queueInfo({}, i, 1, &priority);
                try {
                    device = vk::raii::Device(
                        candidate,
                        vk::DeviceCreateInfo({},
                                             queueInfo,
                                             {},
                                             extensions,
                                             nullptr,
                                             &enabled.get<vk::PhysicalDeviceFeatures2>()));
                    physicalDevice = candidate;
                    queueFamily = i;
                    queue = device.getQueue(i, 0);
                    return;
                } catch (const std::exception &error) {
                    lastFailure = error.what();
                }
            }
        }
        throw std::runtime_error(
            "No Vulkan device supports tensors, shaderTensorAccess, dataGraph, "
            "dataGraphShaderModule, shaderInt8, vulkanMemoryModel and synchronization2 "
            "features with a DataGraph queue" +
            (lastFailure.empty() ? std::string{} : ": " + lastFailure));
    }

    mlsdk::workloadlib::ContextView view() const {
        return {instance, physicalDevice, device, queueFamily, queue};
    }

  private:
    vk::raii::Context loader;
    vk::raii::Instance instance{nullptr};
    vk::raii::PhysicalDevice physicalDevice{nullptr};
    vk::raii::Device device{nullptr};
    vk::raii::Queue queue{nullptr};
    uint32_t queueFamily = 0;
};

} // namespace opk::mlwl
