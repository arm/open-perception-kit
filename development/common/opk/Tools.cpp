/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

#include "Tools.h"

#include "Log.h"
#include "fmt/core.h"
#include "opk/String.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <dlfcn.h>
#include <limits>
#include <memory>
#include <vector>

using namespace opk;

std::string Tools::getLocalIp() {
    // HTTP server must bind to 0.0.0.0 (all interfaces) to accept connections
    // from host machine through Docker port mapping
    return "0.0.0.0";
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#pragma GCC diagnostic pop

Result<DynamicLibraryHandle> Tools::DynamicLibraryOpen(const std::string &name) {
    std::vector<std::string> names;
    names.push_back(name);
    if (opk::utf8::endsWith(name, ".so") == false) {
        names.push_back(name + ".so");
    }

    std::string loadErrors;
    void *handle = nullptr;

    for (const auto &n : names) {
        dlerror(); // NOLINT(concurrency-mt-unsafe)

        handle = dlopen(n.c_str(), RTLD_NOW | RTLD_NODELETE); // NOLINT(concurrency-mt-unsafe)

        if (handle) {
            break;
        }

        const char *err = dlerror(); // NOLINT(concurrency-mt-unsafe)
        if (!err) {
            err = "unknown error";
        }
        loadErrors += fmt::format("ERROR while loading library [{}]: {}\n", n, err);
    }

    if (!handle) {
        opk::log::error("{}", loadErrors);
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::SystemFailure, loadErrors));
    }

    return handle;
}

void Tools::DynamicLibraryClose(DynamicLibraryHandle handle) {
    if (!handle) {
        return;
    }
    dlclose(handle);
}

Result<void *> Tools::DynamicLibraryGetSymbolRaw(DynamicLibraryHandle handle,
                                                 const std::string &symbolName) {
    if (!handle) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::SystemFailure, "Null dynamic library handle"));
    }

    dlerror(); // NOLINT(concurrency-mt-unsafe) clear old errors

    void *sym = dlsym(handle, symbolName.c_str());

    if (const char *err = dlerror(); err != nullptr) { // NOLINT(concurrency-mt-unsafe)
        std::string errorInfo = fmt::format("Symbol [{}] lookup error: {}", symbolName, err);
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::SystemFailure, errorInfo));
    }

    return sym;
}

uint64_t opk::nextObjectId() noexcept {
    static std::atomic<uint64_t> objectIdCounter{1};
    return objectIdCounter.fetch_add(1, std::memory_order_relaxed);
}

namespace {

std::string imageExtension(const std::string &path) {
    const auto separator = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= path.size() ||
        (separator != std::string::npos && dot < separator)) {
        return {};
    }
    return opk::utf8::toLowerAscii(path.substr(dot));
}

bool isSupportedImageExtension(const std::string &extension) {
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg";
}

Result<std::vector<uint8_t>> loadImageFileChannels(const std::string &path,
                                                   size_t &outWidth,
                                                   size_t &outHeight,
                                                   int requestedChannels) {
    outWidth = 0;
    outHeight = 0;

    const std::string extension = imageExtension(path);
    if (!isSupportedImageExtension(extension)) {
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::NotSupported,
            fmt::format("Unsupported image file extension '{}' for '{}'; supported extensions: "
                        ".png, .jpg, .jpeg",
                        extension.empty() ? "<none>" : extension,
                        path)));
    }

    int width = 0;
    int height = 0;
    int channels = 0;

    stbi_uc *rawPixels = stbi_load(path.c_str(), &width, &height, &channels, requestedChannels);
    if (rawPixels == nullptr) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::FileOperationError,
                      fmt::format("Failed to load image file '{}': {}",
                                  path,
                                  stbi_failure_reason() != nullptr ? stbi_failure_reason()
                                                                   : "unknown error")));
    }

    auto pixels = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>(rawPixels, stbi_image_free);

    if (width <= 0 || height <= 0) {
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidData,
            fmt::format(
                "Image file '{}' decoded to invalid dimensions {}x{}", path, width, height)));
    }

    const auto imageWidth = static_cast<size_t>(width);
    const auto imageHeight = static_cast<size_t>(height);
    const auto bytesPerPixel = static_cast<size_t>(requestedChannels);

    if (imageWidth > std::numeric_limits<size_t>::max() / imageHeight ||
        imageWidth * imageHeight > std::numeric_limits<size_t>::max() / bytesPerPixel) {
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidData,
            fmt::format("Image file '{}' dimensions are too large: {}x{}", path, width, height)));
    }

    const size_t byteCount = imageWidth * imageHeight * bytesPerPixel;
    std::vector<uint8_t> result(pixels.get(), pixels.get() + byteCount);

    outWidth = imageWidth;
    outHeight = imageHeight;
    return result;
}

} // namespace

Result<std::vector<uint8_t>>
Tools::loadImageFile(const std::string &path, size_t &outWidth, size_t &outHeight) {
    constexpr int requestedChannels = 3;
    return loadImageFileChannels(path, outWidth, outHeight, requestedChannels);
}

Result<std::vector<uint8_t>>
Tools::loadImageFileBgra(const std::string &path, size_t &outWidth, size_t &outHeight) {
    constexpr int requestedChannels = 4;

    auto rgbaPixels = loadImageFileChannels(path, outWidth, outHeight, requestedChannels);
    if (!rgbaPixels) {
        return rgbaPixels;
    }

    auto &pixels = *rgbaPixels;
    constexpr size_t bytesPerPixel = 4U;
    for (size_t i = 0; i < pixels.size(); i += bytesPerPixel) {
        std::swap(pixels[i + 0U], pixels[i + 2U]);
    }

    return rgbaPixels;
}

bool Tools::savePngFromBgra(const std::string &path, const uint8_t *bgra, int width, int height) {
    if (!bgra || width <= 0 || height <= 0)
        return false;

    std::vector<uint8_t> rgba(size_t(width) * size_t(height) * 4);

    for (int i = 0; i < width * height; ++i) {
        rgba[i * 4 + 0] = bgra[i * 4 + 2]; // R
        rgba[i * 4 + 1] = bgra[i * 4 + 1]; // G
        rgba[i * 4 + 2] = bgra[i * 4 + 0]; // B
        rgba[i * 4 + 3] = bgra[i * 4 + 3]; // A
    }

    // stride = bytes per row
    const int stride = width * 4;
    return stbi_write_png(path.c_str(), width, height, 4, rgba.data(), stride) != 0;
}

static inline int clampInt(int v, int lo, int hi) {
    return std::max(lo, std::min(v, hi));
}

bool Tools::savePngCropFromBgra(const std::string &path,
                                const uint8_t *srcBgra,
                                int srcWidth,
                                int srcHeight,
                                int cropX,
                                int cropY,
                                int cropW,
                                int cropH,
                                int srcStrideBytes) {
    if (!srcBgra || srcWidth <= 0 || srcHeight <= 0) {
        return false;
    }
    if (srcStrideBytes <= 0) {
        srcStrideBytes = srcWidth * 4;
    }

    // If crop dims are non-positive, fail early.
    if (cropW <= 0 || cropH <= 0) {
        return false;
    }

    // Clamp crop rectangle to image bounds.
    int x0 = clampInt(cropX, 0, srcWidth);
    int y0 = clampInt(cropY, 0, srcHeight);
    int x1 = clampInt(cropX + cropW, 0, srcWidth);
    int y1 = clampInt(cropY + cropH, 0, srcHeight);

    int outW = x1 - x0;
    int outH = y1 - y0;
    if (outW <= 0 || outH <= 0) {
        return false;
    }

    std::vector<uint8_t> rgba(size_t(outW) * size_t(outH) * 4);

    // Convert only the cropped region: BGRA -> RGBA
    for (int row = 0; row < outH; ++row) {
        const uint8_t *srcRow =
            srcBgra + (size_t(y0 + row) * size_t(srcStrideBytes)) + (size_t(x0) * 4);
        uint8_t *dstRow = rgba.data() + (size_t(row) * size_t(outW) * 4);

        for (int col = 0; col < outW; ++col) {
            const uint8_t b = srcRow[col * 4 + 0];
            const uint8_t g = srcRow[col * 4 + 1];
            const uint8_t r = srcRow[col * 4 + 2];
            const uint8_t a = srcRow[col * 4 + 3];

            dstRow[col * 4 + 0] = r;
            dstRow[col * 4 + 1] = g;
            dstRow[col * 4 + 2] = b;
            dstRow[col * 4 + 3] = a;
        }
    }

    const int outStride = outW * 4;
    return stbi_write_png(path.c_str(), outW, outH, 4, rgba.data(), outStride) != 0;
}

bool Tools::savePngFromRgbChwF32(const std::string &path,
                                 const float *srcRgbChwF32,
                                 size_t width,
                                 size_t height) {
    if (!srcRgbChwF32 || width == 0 || height == 0) {
        return false;
    }

    std::vector<uint8_t> rgba(width * height * 4);

    auto to_u8 = [](float v) -> uint8_t {
        v = std::clamp(v, 0.0f, 1.0f);
        return static_cast<uint8_t>(std::lround(v * 255.0f));
    };

    const size_t plane = width * height;

    for (size_t i = 0; i < plane; ++i) {
        const float r = srcRgbChwF32[0 * plane + i];
        const float g = srcRgbChwF32[1 * plane + i];
        const float b = srcRgbChwF32[2 * plane + i];

        rgba[i * 4 + 0] = to_u8(r);
        rgba[i * 4 + 1] = to_u8(g);
        rgba[i * 4 + 2] = to_u8(b);
        rgba[i * 4 + 3] = 255;
    }

    return stbi_write_png(path.c_str(),
                          static_cast<int>(width),
                          static_cast<int>(height),
                          4,
                          rgba.data(),
                          static_cast<int>(width) * 4) != 0;
}
