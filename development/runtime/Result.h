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

#include <cstdint>
#include <source_location>
#include <string>

#include <tl/expected.hpp>

namespace opk::runtime {

/**
 * @brief Stable public runtime API error categories.
 *
 * The runtime API intentionally exposes a smaller subset than the internal runtime.
 * Detailed subsystem errors are converted to one of these categories while the
 * human-readable detail remains available in Error::info.
 */
enum class ErrorFlag {
    /// No error.
    Ok = 0,
    /// Caller supplied an invalid argument or called an operation in an invalid state.
    InvalidArgument,
    /// Pipeline text, metadata, or topology is invalid for the API operation.
    InvalidPipeline,
    /// Referenced file or directory does not exist.
    FileNotFound,
    /// Text, JSON, or pipeline-description parsing failed.
    ParseError,
    /// Pipeline execution or external runtime operation failed.
    RuntimeError,
    /// Inference startup, model loading, or inference execution failed.
    InferenceError,
    /// Requested operation is not supported by this runtime API.
    NotSupported,
    /// Unexpected implementation failure not covered by a more specific category.
    InternalError
};

/**
 * @brief Public runtime API error payload used by runtime::Result.
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
    Error(ErrorFlag f, std::string i, std::source_location loc = std::source_location::current());

    /// Error category.
    ErrorFlag flag = ErrorFlag::InternalError;

    /// Human-readable error detail.
    std::string info;

    /// File where the error was produced.
    std::string file;

    /// Function where the error was produced.
    std::string function;

    /// Source line where the error was produced.
    uint32_t line = 0;

    /**
     * @brief Renders this error as a readable string.
     * @return Formatted message including category, reason, and optional location.
     */
    std::string toString() const;
};

/**
 * @brief Expected-like result carrying either a value of type T or a runtime::Error.
 */
template <typename T> using Result = tl::expected<T, Error>;

} // namespace opk::runtime
