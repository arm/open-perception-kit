/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <cstdint>
#include <source_location>
#include <string>

#include <tl/expected.hpp>

namespace pek::api {

/**
 * @brief Public API error categories.
 *
 * These values intentionally mirror the runtime error categories, but they live
 * in the API namespace so application headers do not need to include internal
 * PEK runtime headers.
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
 * @brief Public API error payload used by api::Result.
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
    ErrorFlag flag = ErrorFlag::GenericError;

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
 * @brief Expected-like result carrying either a value of type T or an api::Error.
 */
template <typename T> using Result = tl::expected<T, Error>;

} // namespace pek::api
