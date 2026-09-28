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

#pragma once

#include "runtime/Result.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace opk::runtime {

/**
 * @brief Small public utility helpers for applications using the OPK runtime layer.
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

} // namespace opk::runtime
