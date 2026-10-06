/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

#include "opk/Result.h"

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <type_traits>
#include <vector>

namespace opk {

typedef void *DynamicLibraryHandle;

/**
 * @brief Utility helpers shared across runtime components.
 */
struct Tools {
    /**
     * @brief Returns the bind address used by local HTTP/WebRTC services.
     * @return Address string suitable for binding service sockets.
     */
    static std::string getLocalIp();

    /**
     * @brief Opens a shared library by name.
     * @param name Library file name or path. If no .so suffix is provided, .so is also tried.
     * The library remains resident until process exit so C++ plugin globals cannot be unloaded
     * while references to them may still exist.
     * @return Loaded dynamic library handle on success, error details on failure.
     */
    static Result<DynamicLibraryHandle> DynamicLibraryOpen(const std::string &name);

    /**
     * @brief Closes a previously opened shared library handle without unloading the library.
     * @param handle Dynamic library handle returned by DynamicLibraryOpen.
     */
    static void DynamicLibraryClose(void *handle);

    /**
     * @brief Resolves a raw symbol pointer from a loaded shared library.
     * @param handle Dynamic library handle returned by DynamicLibraryOpen.
     * @param symbolName Symbol to look up.
     * @return Raw symbol address on success, error details on failure.
     */
    static Result<void *> DynamicLibraryGetSymbolRaw(DynamicLibraryHandle handle,
                                                     const std::string &symbolName);

    /**
     * @brief Resolves a typed function pointer from a loaded shared library.
     * @tparam FuncPtr Function pointer type to cast the resolved symbol to.
     * @param handle Dynamic library handle returned by DynamicLibraryOpen.
     * @param symbolName Symbol to look up.
     * @return Typed function pointer on success, error details on failure.
     */
    template <typename FuncPtr>
    static Result<FuncPtr> DynamicLibraryGetSymbol(DynamicLibraryHandle handle,
                                                   const std::string &symbolName) {
        static_assert(std::is_pointer_v<FuncPtr>, "FuncPtr must be a pointer type");
        static_assert(std::is_function_v<std::remove_pointer_t<FuncPtr>>,
                      "FuncPtr must be a function pointer type, e.g. int(*)(int)");

        auto raw = DynamicLibraryGetSymbolRaw(handle, symbolName);
        if (!raw) {
            return tl::unexpected(raw.error());
        }

        return reinterpret_cast<FuncPtr>(*raw);
    }

    /**
     * @brief Loads a PNG or JPEG image file into tightly packed RGB pixels.
     *
     * The file format is inferred from the file name extension before decoding.
     * Supported extensions are `.png`, `.jpg`, and `.jpeg`, case-insensitively.
     * The returned buffer uses 8-bit RGB HWC order and contains
     * `outWidth * outHeight * 3` bytes.
     *
     * @param path Image file path.
     * @param outWidth Receives the decoded image width in pixels. Set to 0 on failure.
     * @param outHeight Receives the decoded image height in pixels. Set to 0 on failure.
     * @return RGB pixel buffer on success, error details on unsupported extension or load failure.
     */
    static Result<std::vector<uint8_t>>
    loadImageFile(const std::string &path, size_t &outWidth, size_t &outHeight);

    /**
     * @brief Loads a PNG or JPEG image file into tightly packed BGRA pixels.
     *
     * The file format is inferred from the file name extension before decoding.
     * Supported extensions are `.png`, `.jpg`, and `.jpeg`, case-insensitively.
     * The returned buffer uses 8-bit BGRA HWC order and contains
     * `outWidth * outHeight * 4` bytes. PNG alpha is preserved; formats without
     * alpha receive an opaque alpha channel.
     *
     * @param path Image file path.
     * @param outWidth Receives the decoded image width in pixels. Set to 0 on failure.
     * @param outHeight Receives the decoded image height in pixels. Set to 0 on failure.
     * @return BGRA pixel buffer on success, error details on unsupported extension or load failure.
     */
    static Result<std::vector<uint8_t>>
    loadImageFileBgra(const std::string &path, size_t &outWidth, size_t &outHeight);

    /**
     * @brief Saves a BGRA image as PNG.
     * @param path Output PNG path.
     * @param bgra Source image data in BGRA format.
     * @param width Source image width in pixels.
     * @param height Source image height in pixels.
     * @return true on success, false on invalid input or write failure.
     */
    static bool
    savePngFromBgra(const std::string &path, const uint8_t *bgra, int width, int height);

    /**
     * @brief Saves a cropped BGRA region as PNG.
     * @param path Output PNG path.
     * @param srcBgra Source image data in BGRA format.
     * @param srcWidth Source image width in pixels.
     * @param srcHeight Source image height in pixels.
     * @param cropX Crop rectangle x origin in pixels.
     * @param cropY Crop rectangle y origin in pixels.
     * @param cropW Crop rectangle width in pixels.
     * @param cropH Crop rectangle height in pixels.
     * @param srcStrideBytes Source row stride in bytes; if 0, width*4 is used.
     * @return true on success, false on invalid input or write failure.
     */
    static bool savePngCropFromBgra(const std::string &path,
                                    const uint8_t *srcBgra,
                                    int srcWidth,
                                    int srcHeight,
                                    int cropX,
                                    int cropY,
                                    int cropW,
                                    int cropH,
                                    int srcStrideBytes = 0);

    /**
     * @brief Saves an RGB float32 CHW tensor as PNG.
     * @param path Output PNG path.
     * @param srcRgbF32 Source tensor data in CHW order with values expected in [0,1].
     * @param srcWidth Tensor/image width in pixels.
     * @param srcHeight Tensor/image height in pixels.
     * @return true on success, false on invalid input or write failure.
     */
    static bool savePngFromRgbChwF32(const std::string &path,
                                     const float *srcRgbF32,
                                     size_t srcWidth,
                                     size_t srcHeight);
};

/**
 * @brief Returns the next process-local object ID.
 * @return Monotonically increasing object ID value.
 */
uint64_t nextObjectId() noexcept;

/**
 * @brief UTC wall-clock time helpers.
 */
struct Time {
    /**
     * @brief Returns current UTC time in nanoseconds since Unix epoch.
     * @return UTC timestamp in nanoseconds.
     */
    static uint64_t utcNano() noexcept {
        timespec ts{};
        clock_gettime(CLOCK_REALTIME, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
    }

    /**
     * @brief Returns current UTC time in microseconds since Unix epoch.
     * @return UTC timestamp in microseconds.
     */
    static uint64_t utcMicro() noexcept {
        return utcNano() / 1000ULL;
    }

    /**
     * @brief Returns current UTC time in milliseconds since Unix epoch.
     * @return UTC timestamp in milliseconds.
     */
    static uint64_t utcMs() noexcept {
        return utcNano() / 1000000ULL;
    }

    /**
     * @brief Returns current UTC time in seconds since Unix epoch.
     * @return UTC timestamp in seconds.
     */
    static uint64_t utcSec() noexcept {
        return utcNano() / 1000000000ULL;
    }
};

} // namespace opk
