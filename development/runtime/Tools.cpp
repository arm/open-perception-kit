/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "runtime/Tools.h"

#include "pek/Tools.h"

#include <fmt/core.h>
#include <magic_enum/magic_enum.hpp>

#include <utility>

namespace pek::runtime {
namespace {

ErrorFlag mapInternalErrorFlag(pek::ErrorFlag flag) noexcept {
    switch (flag) {
    case pek::ErrorFlag::Ok:
        return ErrorFlag::Ok;
    case pek::ErrorFlag::FileNotFound:
        return ErrorFlag::FileNotFound;
    case pek::ErrorFlag::ParseError:
        return ErrorFlag::ParseError;
    case pek::ErrorFlag::NotSupported:
        return ErrorFlag::NotSupported;
    case pek::ErrorFlag::InferenceRtStartupError:
    case pek::ErrorFlag::InferenceRtModelLoadError:
    case pek::ErrorFlag::InferenceRtInferenceError:
    case pek::ErrorFlag::InferenceRtGenericError:
    case pek::ErrorFlag::ModelInspectError:
        return ErrorFlag::InferenceError;
    case pek::ErrorFlag::SystemFailure:
        return ErrorFlag::RuntimeError;
    case pek::ErrorFlag::InvalidOpChain:
    case pek::ErrorFlag::InvalidData:
    case pek::ErrorFlag::FileOperationError:
    case pek::ErrorFlag::ErrorWithSrcSetup:
    case pek::ErrorFlag::ErrorWithDstSetup:
    case pek::ErrorFlag::SizeMismatch:
    case pek::ErrorFlag::ImageModelDimensionError:
    case pek::ErrorFlag::ImageDimensionError:
    case pek::ErrorFlag::TensorError:
        return ErrorFlag::InvalidPipeline;
    case pek::ErrorFlag::GenericError:
        return ErrorFlag::InternalError;
    }
    return ErrorFlag::InternalError;
}

Error mapInternalError(const pek::Error &error) {
    const auto internalFlagName = magic_enum::enum_name(error.flag);
    Error runtimeError(mapInternalErrorFlag(error.flag),
                       internalFlagName.empty()
                           ? error.info
                           : fmt::format("{}: {}", internalFlagName, error.info));
    runtimeError.file = error.file;
    runtimeError.function = error.function;
    runtimeError.line = error.line;
    return runtimeError;
}

} // namespace

Result<std::vector<std::uint8_t>>
Tools::loadImageFileBgra(const std::string &path, std::size_t &outWidth, std::size_t &outHeight) {
    auto result = pek::Tools::loadImageFileBgra(path, outWidth, outHeight);
    if (!result) {
        return tl::make_unexpected(mapInternalError(result.error()));
    }
    return std::move(*result);
}

} // namespace pek::runtime
