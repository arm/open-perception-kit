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

#define EGL_EGLEXT_PROTOTYPES
#include "LinuxVideo.h"

#include <EGL/eglext.h>
#include <GLES2/gl2ext.h>
#include <drm_fourcc.h>
#include <gbm.h>

#include <algorithm>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unistd.h>

namespace opk::gles {
namespace {
bool hasExtension(const char *const extensions, const std::string_view name) {
    const std::string list{extensions ? extensions : ""};
    return (" " + list + " ").find(" " + std::string(name) + " ") != std::string::npos;
}
} // namespace

struct LinuxDisplay::State {
    int fd{-1};
    gbm_device *device{nullptr};
    EGLDisplay display{EGL_NO_DISPLAY};
    ~State() {
        if (display != EGL_NO_DISPLAY) {
            eglTerminate(display);
        }
        if (device) {
            gbm_device_destroy(device);
        }
        if (fd >= 0) {
            close(fd);
        }
    }
};

LinuxDisplay::LinuxDisplay(const std::string &path) : state{std::make_unique<State>()} {
    state->fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (state->fd < 0) {
        throw std::runtime_error("Cannot open " + path + "; check render-device access");
    }
    state->device = gbm_create_device(state->fd);
    if (!state->device) {
        throw std::runtime_error("Cannot create a GBM device for " + path);
    }
    state->display = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, state->device, nullptr);
    if (!eglInitialize(state->display, nullptr, nullptr)) {
        throw std::runtime_error("Cannot initialize EGL for " + path);
    }
}
LinuxDisplay::~LinuxDisplay() = default;
EGLDisplay LinuxDisplay::get() const {
    return state->display;
}

std::vector<std::string> LinuxDisplay::renderDevices() {
    std::vector<std::string> devices;
    if (std::filesystem::exists("/dev/dri")) {
        for (const auto &entry : std::filesystem::directory_iterator("/dev/dri")) {
            if (entry.path().filename().string().starts_with("renderD")) {
                devices.push_back(entry.path().string());
            }
        }
    }
    std::sort(devices.begin(), devices.end());
    return devices;
}

ImportedVideo::ImportedVideo(const EGLDisplay display) : display{display} {}
ImportedVideo::~ImportedVideo() {
    glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
    for (const auto image : images) {
        if (image != EGL_NO_IMAGE) {
            eglDestroyImage(display, image);
        }
    }
}
void ImportedVideo::bind() const {
    for (size_t index{0}; index < planeCount; ++index) {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(index));
        glBindTexture(GL_TEXTURE_2D, textures[index]);
    }
}

std::unique_ptr<ImportedVideo> ImportedVideo::tryImport(const mediaio::VideoFrame &frame,
                                                        const EGLDisplay display) {
    const char *const extensions{eglQueryString(display, EGL_EXTENSIONS)};
    if (frame.memoryType() != MemoryType::DmaBuf ||
        !hasExtension(extensions, "EGL_EXT_image_dma_buf_import")) {
        return nullptr;
    }
    const auto imageTarget{reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
        eglGetProcAddress("glEGLImageTargetTexture2DOES"))};
    if (!imageTarget) {
        return nullptr;
    }
    const bool modifiers{hasExtension(extensions, "EGL_EXT_image_dma_buf_import_modifiers")};
    std::vector<uint32_t> formats;
    switch (frame.format()) {
    case RawImagePixelFormat::Bgra:
        formats = {DRM_FORMAT_ARGB8888};
        break;
    case RawImagePixelFormat::Rgb:
        formats = {DRM_FORMAT_BGR888};
        break;
    case RawImagePixelFormat::Nv12:
        formats = {DRM_FORMAT_R8, DRM_FORMAT_GR88};
        break;
    case RawImagePixelFormat::I420:
        formats = {DRM_FORMAT_R8, DRM_FORMAT_R8, DRM_FORMAT_R8};
        break;
    case RawImagePixelFormat::Yuy2:
        formats = {DRM_FORMAT_GR88};
        break;
    default:
        return nullptr;
    }
    if (frame.planeCount() != formats.size()) {
        return nullptr;
    }
    auto imported{std::make_unique<ImportedVideo>(display)};
    imported->planeCount = formats.size();
    constexpr uint32_t chromaSubsampling{2};
    for (size_t index{0}; index < formats.size(); ++index) {
        const auto &plane{frame.planes()[index]};
        if (!plane.hasDmaBuf() || !plane.canRead() || plane.sync().hasAcquireFence() ||
            plane.sync().hasReleaseFence() ||
            plane.offset() > static_cast<size_t>(std::numeric_limits<EGLint>::max()) ||
            plane.strideBytes() > static_cast<uint32_t>(std::numeric_limits<EGLint>::max())) {
            return nullptr;
        }
        const auto width{index == 0 ? frame.width() : (frame.width() + 1) / chromaSubsampling};
        const auto height{index == 0 ? frame.height() : (frame.height() + 1) / chromaSubsampling};
        std::vector<EGLAttrib> attributes{EGL_WIDTH,
                                          width,
                                          EGL_HEIGHT,
                                          height,
                                          EGL_LINUX_DRM_FOURCC_EXT,
                                          formats[index],
                                          EGL_DMA_BUF_PLANE0_FD_EXT,
                                          plane.fd(),
                                          EGL_DMA_BUF_PLANE0_OFFSET_EXT,
                                          static_cast<EGLAttrib>(plane.offset()),
                                          EGL_DMA_BUF_PLANE0_PITCH_EXT,
                                          plane.strideBytes()};
        if (modifiers) {
            attributes.insert(
                attributes.end(),
                {EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, 0, EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, 0});
        }
        attributes.push_back(EGL_NONE);
        imported->images[index] = eglCreateImage(
            display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attributes.data());
        if (imported->images[index] == EGL_NO_IMAGE) {
            return nullptr;
        }
        glGenTextures(1, &imported->textures[index]);
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(index));
        glBindTexture(GL_TEXTURE_2D, imported->textures[index]);
        imageTarget(GL_TEXTURE_2D, imported->images[index]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        if (glGetError() != GL_NO_ERROR) {
            return nullptr;
        }
    }
    return imported;
}
} // namespace opk::gles
