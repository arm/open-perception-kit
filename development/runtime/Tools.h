/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "runtime/Result.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pek::runtime {

/**
 * @brief Small public utility helpers for applications using the PEK runtime layer.
 *
 * Tools intentionally exposes only application-facing helpers and keeps internal
 * runtime headers out of the public API surface.
 */
struct Tools {
    /**
     * @brief Loads a PNG or JPEG image file into tightly packed BGRA pixels.
     *
     * The file format is inferred from the filename extension. Supported
     * extensions are `.png`, `.jpg`, and `.jpeg`, case-insensitively. The
     * returned buffer uses 8-bit BGRA HWC order and contains
     * `outWidth * outHeight * 4` bytes.
     *
     * @param path Image file path.
     * @param outWidth Receives the decoded image width in pixels. Set to 0 on failure.
     * @param outHeight Receives the decoded image height in pixels. Set to 0 on failure.
     * @return BGRA pixel buffer on success, or an API error on failure.
     */
    static Result<std::vector<std::uint8_t>>
    loadImageFileBgra(const std::string &path, std::size_t &outWidth, std::size_t &outHeight);
};

} // namespace pek::runtime
