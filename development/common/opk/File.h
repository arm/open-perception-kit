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
#include "opk/String.h"
#include "tl/expected.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace opk {

/**
 * @brief Filesystem and file-loading helpers.
 */
struct fs {

    /**
     * @brief Text BOM classification.
     */
    enum class Bom {
        None,
        Utf8,
        Utf16Le,
        Utf16Be,
        Utf32Le,
        Utf32Be,
    };

    /**
     * @brief Returns true if a filesystem path exists.
     * @param path File or directory path.
     * @return true if the path exists, false otherwise.
     */
    static bool exists(const std::string &path) {
        if (std::filesystem::exists(path))
            return true;
        return false;
    }

    /**
     * @brief Returns the size of a file in bytes.
     * @param path File path.
     * @return File size on success, error when the file is missing.
     */
    static opk::Result<uint64_t> fileSize(const std::string &path) {
        if (!exists(path)) {
            return tl::unexpected(opk::Error(opk::ErrorFlag::FileNotFound, path));
        }
        return (uint64_t)std::filesystem::file_size(path);
    }

    /**
     * @brief Loads a file if it exists, otherwise returns an empty byte buffer.
     * @param path File path.
     * @return Loaded file bytes, or an empty vector when the file is missing or cannot be read.
     */
    static std::vector<uint8_t> loadIfExistsOrEmpty(const std::string &path) {
        if (!exists(path)) {
            return std::vector<uint8_t>{};
        }

        auto loadResult = load(path, false);
        if (!loadResult.has_value())
            return std::vector<uint8_t>{};

        return *loadResult;
    }

    /**
     * @brief Backwards-compatible alias for loadIfExistsOrEmpty().
     * @param path File path.
     * @return Loaded file bytes, or an empty vector when the file is missing or cannot be read.
     */
    static std::vector<uint8_t> loadOrEmpty(const std::string &path) {
        return loadIfExistsOrEmpty(path);
    }

    /**
     * @brief Loads a file as raw bytes.
     * @param path File path.
     * @param returnEmptyIfNotFound When true, missing files produce an empty buffer instead of an
     * error.
     * @return File bytes on success, or an error when the file cannot be read.
     */
    static opk::Result<std::vector<uint8_t>> load(const std::string &path,
                                                  bool returnEmptyIfNotFound = true) {
        if (!exists(path)) {
            if (returnEmptyIfNotFound) {
                return std::vector<uint8_t>{};
            }
            return tl::unexpected(opk::Error(opk::ErrorFlag::FileNotFound, path));
        }

        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            return tl::unexpected(opk::Error(opk::ErrorFlag::FileOperationError, path));
        }

        const std::streamsize size = file.tellg();
        if (size < 0) {
            return tl::unexpected(opk::Error(opk::ErrorFlag::FileOperationError, path));
        }

        std::vector<uint8_t> buffer(static_cast<size_t>(size));
        file.seekg(0, std::ios::beg);

        if (!file.read(reinterpret_cast<char *>(buffer.data()), size)) {
            return tl::unexpected(opk::Error(opk::ErrorFlag::FileOperationError, path));
        }

        return buffer;
    }

    /**
     * @brief Loads a UTF-8 text file, returning a caller-provided fallback on error.
     * @param path File path.
     * @param defaultResult Value returned when the file cannot be loaded as UTF-8 text.
     * @return Loaded text or @p defaultResult.
     */
    static std::string loadTextOrDefault(const std::string &path,
                                         const std::string &defaultResult) {
        opk::Result<std::string> loadResult = loadText(path);
        if (loadResult.has_value() == false)
            return defaultResult;
        return loadResult.value();
    }

    /**
     * @brief Loads a text file and validates that it is UTF-8 encoded.
     * @param path File path.
     * @return File contents on success, or an error when the file is missing, unreadable, or not
     * UTF-8.
     */
    static opk::Result<std::string> loadText(const std::string &path) {

        if (false == exists(path)) {
            return tl::unexpected(opk::Error(opk::ErrorFlag::FileNotFound, path));
        }

        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) {
            return tl::unexpected(opk::Error(opk::ErrorFlag::FileOperationError, path));
        }

        const auto size = f.tellg();
        if (size < 0) {
            return tl::unexpected(opk::Error(opk::ErrorFlag::FileOperationError, path));
        }

        std::string out(size, '\0');
        f.seekg(0);
        if (!f.read(out.data(), size)) {
            return tl::unexpected(opk::Error(opk::ErrorFlag::FileOperationError, path));
        }

        Bom bom = detectBom(out);
        if (bom == Bom::None || bom == Bom::Utf8) {
            if (bom == Bom::Utf8) {
                out.erase(0, 3);
            }
        } else {
            return tl::unexpected(
                opk::Error(opk::ErrorFlag::InvalidData,
                           fmt::format("Text file [{}] must be a UTF-8 encoded text file", path)));
        }

        if (codepoint::validate(out.c_str()) == false) {
            return tl::unexpected(
                opk::Error(opk::ErrorFlag::InvalidData,
                           fmt::format("Text file [{}] does not contain valid UTF-8 data", path)));
        }

        return out;
    }

    /**
     * @brief Detects the Unicode BOM prefix of a byte string.
     * @param s Raw file content.
     * @return Detected BOM classification, or Bom::None when no BOM is present.
     */
    static Bom detectBom(const std::string &s) {
        if (s.size() >= 4 && (unsigned char)s[0] == 0xFF && (unsigned char)s[1] == 0xFE &&
            (unsigned char)s[2] == 0x00 && (unsigned char)s[3] == 0x00)
            return Bom::Utf32Le;

        if (s.size() >= 4 && (unsigned char)s[0] == 0x00 && (unsigned char)s[1] == 0x00 &&
            (unsigned char)s[2] == 0xFE && (unsigned char)s[3] == 0xFF)
            return Bom::Utf32Be;

        if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
            (unsigned char)s[2] == 0xBF)
            return Bom::Utf8;

        if (s.size() >= 2 && (unsigned char)s[0] == 0xFF && (unsigned char)s[1] == 0xFE)
            return Bom::Utf16Le;

        if (s.size() >= 2 && (unsigned char)s[0] == 0xFE && (unsigned char)s[1] == 0xFF)
            return Bom::Utf16Be;

        return Bom::None;
    }
};
} // namespace opk
