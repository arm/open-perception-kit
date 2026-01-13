#pragma once

#include <source_location>
#include <string>

#include "fmt/format.h"
#include <fmt/color.h>
#include <fmt/core.h>

#include "magic_enum/magic_enum.hpp"

#include "tl/expected.hpp"

namespace amp {

enum class ErrorFlag {
    Ok = 0, // but why?
    FileNotFound,
    FileOperationError,
    InvalidData,
    GenericError,
    ModelInspectError,
    OnnxStartupException,
    OnnxModelLoadException,
    OnnxInferenceException,
    NotSupported,
    ErrorWithSrcSetup,
    ErrorWithDstSetup,
    SizeMismatch,
    ImageModelDimensionError,
    ImageDimensionError,
    TensorError,
    ParseError
};

struct Error {

    Error() {}

    Error(ErrorFlag flag,
          const std::string &info,
          const std::source_location &location = std::source_location())
        : flag(flag), info(info), sourceLocation(location) {}

    const ErrorFlag flag = ErrorFlag::GenericError;
    const std::string info;
    const std::source_location sourceLocation;

    std::string toString() const;
};

template <typename T> using Result = tl::expected<T, Error>;

} // namespace amp

#define AMP_ERROR(flag, info) ::amp::Error(flag, info, std::source_location::current())
