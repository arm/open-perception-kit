/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <source_location>
#include <string>

#include <fmt/color.h>
#include <fmt/core.h>
#include <fmt/format.h>

#include <magic_enum/magic_enum.hpp>
#include <tl/expected.hpp>

namespace amp {

enum class ErrorFlag {
    Ok = 0,
    FileNotFound,
    FileOperationError,
    InvalidData,
    GenericError,
    ModelInspectError,
    OnnxStartupException,
    OnnxModelLoadException,
    OnnxInferenceException,
    ExecuTorchError,
    NotSupported,
    ErrorWithSrcSetup,
    ErrorWithDstSetup,
    SizeMismatch,
    ImageModelDimensionError,
    ImageDimensionError,
    SystemFailure,
    TensorError,
    ParseError,
    InvalidOpChain
};

struct Error {

    Error() = default;

    Error(ErrorFlag f, std::string i, std::source_location loc = std::source_location::current())
        : flag(f), info(std::move(i)), file(loc.file_name()), function(loc.function_name()),
          line(loc.line()) {}

    ErrorFlag flag = ErrorFlag::GenericError;
    std::string info;

    // Extracted from source_location (safe across SO unload)
    std::string file;
    std::string function;
    uint32_t line = 0;

    std::string toString() const;
};

template <typename T> using Result = tl::expected<T, Error>;

} // namespace amp

#define AMP_ERROR(flag, info) ::amp::Error((flag), (info), std::source_location::current())
