/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Result.h"

#include <atomic>
#include <cstdint>
#include <ctime>
#include <string>

namespace pek {

typedef void *DynamicLibraryHandle;

/**
 * @brief Utility helpers shared across runtime components.
 */
struct Tools {
    /**
     * @brief Opens a shared library by name.
     * @param name Library file name or path. If no .so suffix is provided, .so is also tried.
     * @return Loaded dynamic library handle on success, error details on failure.
     */
    static Result<DynamicLibraryHandle> DynamicLibraryOpen(const std::string &name);

    /**
     * @brief Closes a previously opened shared library handle.
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
            return tl::make_unexpected(raw.error());
        }

        return reinterpret_cast<FuncPtr>(*raw);
    }

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
 * @brief Monotonic process-local UUID-like counter.
 */
struct Uuid {
    /**
     * @brief Returns the next monotonically increasing id.
     * @return Next id value.
     */
    static uint64_t next() noexcept {
        return counter.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    static std::atomic<uint64_t> counter;
};

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

} // namespace pek
