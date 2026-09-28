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

#include <cstdint>
#include <source_location>
#include <string>

#include <fmt/color.h>
#include <fmt/core.h>
#include <fmt/format.h>

#include <magic_enum/magic_enum.hpp>
#include <tl/expected.hpp>

namespace opk {

/**
 * @brief Canonical error categories used across the runtime.
 */
enum class ErrorFlag {
    /// No error.
    Ok = 0,
    /// Referenced file does not exist.
    FileNotFound,
    /// Generic filesystem read/write/open failure.
    FileOperationError,
    /// Input data is malformed or violates expected constraints.
    InvalidData,
    /// Unclassified error condition.
    GenericError,
    /// Model inspection/parsing failed.
    ModelInspectError,
    /// Inference runtime failed during startup/initialization.
    InferenceRtStartupError,
    /// Inference runtime failed while loading a model.
    InferenceRtModelLoadError,
    /// Inference runtime failed while running inference.
    InferenceRtInferenceError,
    /// Inference runtime error not covered by a dedicated category.
    InferenceRtGenericError,
    /// Requested operation is not supported.
    NotSupported,
    /// Source tensor or source setup is invalid.
    ErrorWithSrcSetup,
    /// Destination tensor or destination setup is invalid.
    ErrorWithDstSetup,
    /// Shape/size mismatch between expected and actual values.
    SizeMismatch,
    /// Model-defined image dimensions are invalid for current processing.
    ImageModelDimensionError,
    /// Input/output image dimensions are invalid.
    ImageDimensionError,
    /// Operating-system/runtime level failure.
    SystemFailure,
    /// Tensor creation/access/interpretation error.
    TensorError,
    /// Text/binary parsing failure.
    ParseError,
    /// OpChain descriptor or execution graph is invalid.
    InvalidOpChain
};

/**
 * @brief Error payload used as the failure type for Result<T>.
 */
struct Error {

    /**
     * @brief Constructs a default generic error.
     */
    Error() = default;

    /**
     * @brief Constructs an error with category, message, and optional source location.
     * @param f Error category.
     * @param i Human-readable detail message.
     * @param loc Call-site location metadata.
     */
    Error(ErrorFlag f, std::string i, std::source_location loc = std::source_location::current())
        : flag(f), info(std::move(i)), file(loc.file_name()), function(loc.function_name()),
          line(loc.line()) {}

    /// Error category.
    ErrorFlag flag = ErrorFlag::GenericError;

    /// Human-readable error detail.
    std::string info;

    // Extracted from source_location (safe across SO unload)
    /// File where the error was produced.
    std::string file;

    /// Function where the error was produced.
    std::string function;

    /// Source line where the error was produced.
    uint32_t line = 0;

    /**
     * @brief Renders this error as a formatted string.
     * @return Formatted message including category, reason, and optional location.
     */
    std::string toString() const;
};

/**
 * @brief Expected-like result carrying either a value of type T or a opk::Error.
 */
template <typename T> using Result = tl::expected<T, Error>;

} // namespace opk

/**
 * @brief Convenience macro to build opk::Error with automatic call-site location.
 */
#define OPK_ERROR(flag, info) ::opk::Error((flag), (info), std::source_location::current())
