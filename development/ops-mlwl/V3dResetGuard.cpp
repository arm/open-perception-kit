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
#include "V3dResetGuard.h"

#include <vulkan/vulkan_raii.hpp>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef __linux__
#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#endif

namespace opk::mlwl {

#ifdef __linux__
namespace {

uint64_t readResetCounter(const int fd) {
    // Linux v3d_drm.h UAPI; use libc ioctl without a libdrm dependency.
    // https://github.com/torvalds/linux/blob/5774b3cfdedb3624ef0d2c82cccbfd61bcb60fd5/include/uapi/drm/v3d_drm.h
    struct GetParam {
        uint32_t param;
        uint32_t pad;
        uint64_t value;
    };
    static_assert(sizeof(GetParam) == 16 && offsetof(GetParam, value) == 8);
    constexpr uint32_t globalResetCounter = 15;
    GetParam query{globalResetCounter, 0, 0};
    int result;
    do {
        result = ::ioctl(fd, _IOWR('d', 0x44, GetParam), &query);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        throw std::system_error(errno, std::generic_category(), "Reading V3D global reset counter");
    }
    return query.value;
}

} // namespace
#endif

V3dResetGuard::V3dResetGuard(ReadCounter counter)
    : readCounter(std::move(counter)), generation(readCounter()) {}

V3dResetGuard::~V3dResetGuard() {
#ifdef __linux__
    if (descriptor >= 0) {
        ::close(descriptor);
    }
#endif
}

void V3dResetGuard::check() const {
    if (readCounter() != generation) {
        throw V3dReset();
    }
}

std::unique_ptr<V3dResetGuard> V3dResetGuard::create(const vk::raii::PhysicalDevice &device) {
#ifdef __linux__
    const auto properties =
        device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDriverProperties>();
    if (properties.get<vk::PhysicalDeviceDriverProperties>().driverID != vk::DriverId::eMesaV3Dv) {
        return nullptr;
    }
    const auto extensions = device.enumerateDeviceExtensionProperties();
    if (!std::ranges::any_of(extensions, [](const auto &extension) {
            return std::string_view(extension.extensionName.data()) ==
                   VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME;
        })) {
        throw std::runtime_error("V3D reset protection requires VK_EXT_physical_device_drm");
    }
    const auto drmProperties =
        device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDrmPropertiesEXT>();
    const auto &drm = drmProperties.get<vk::PhysicalDeviceDrmPropertiesEXT>();
    if (!drm.hasRender || drm.renderMajor < 0 || drm.renderMinor < 0) {
        throw std::runtime_error("V3D reset protection requires the selected DRM render node");
    }
    const auto path = "/dev/dri/renderD" + std::to_string(drm.renderMinor);
    const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "Opening V3D render node " + path);
    }
    try {
        struct stat info{};
        if (::fstat(fd, &info) != 0) {
            throw std::system_error(errno, std::generic_category(), "Checking V3D render node");
        }
        if (!S_ISCHR(info.st_mode) ||
            static_cast<int64_t>(major(info.st_rdev)) != drm.renderMajor ||
            static_cast<int64_t>(minor(info.st_rdev)) != drm.renderMinor) {
            throw std::runtime_error("V3D render node does not match the Vulkan physical device");
        }
        auto guard = std::make_unique<V3dResetGuard>([fd] { return readResetCounter(fd); });
        guard->descriptor = fd;
        return guard;
    } catch (...) {
        ::close(fd);
        throw;
    }
#else
    static_cast<void>(device);
    return nullptr;
#endif
}

} // namespace opk::mlwl
