#pragma once

#include <string>
#include <source_location>

#include "fmt/format.h"
#include <fmt/core.h>
#include <fmt/color.h>

#include "magic_enum/magic_enum.hpp"

#include "tl/expected.hpp"

namespace amp {

    enum class ErrorFlag {
        Ok = 0, // but why?
        FileNotFound,
        InvalidData,
        GenericError,
        ModelInspectError,
        OnnxLowLevelError
    };

    struct Error {

        Error() { }

        Error(ErrorFlag flag, const std::string& info, const std::source_location& location = std::source_location()) :
            flag(flag), 
            info(info),
            sourceLocation(location)
        { 
        }

        ErrorFlag flag = ErrorFlag::GenericError;
        std::string info;
        std::source_location sourceLocation;

        std::string toString() const;

    };

    template <typename T>
    using Result = tl::expected<T, Error>;    

}

#define AMP_ERROR(flag, info) ::amp::Error(flag, info, std::source_location::current())

