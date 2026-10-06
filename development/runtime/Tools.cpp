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

#include "runtime/Tools.h"

#include "opk/Tools.h"

#include <fmt/core.h>
#include <magic_enum/magic_enum.hpp>

#include <utility>

namespace opk::runtime {
namespace {

ErrorFlag mapInternalErrorFlag(opk::ErrorFlag flag) noexcept {
    switch (flag) {
    case opk::ErrorFlag::Ok:
        return ErrorFlag::Ok;
    case opk::ErrorFlag::FileNotFound:
        return ErrorFlag::FileNotFound;
    case opk::ErrorFlag::ParseError:
        return ErrorFlag::ParseError;
    case opk::ErrorFlag::NotSupported:
        return ErrorFlag::NotSupported;
    case opk::ErrorFlag::InferenceRtStartupError:
    case opk::ErrorFlag::InferenceRtModelLoadError:
    case opk::ErrorFlag::InferenceRtInferenceError:
    case opk::ErrorFlag::InferenceRtGenericError:
    case opk::ErrorFlag::ModelInspectError:
        return ErrorFlag::InferenceError;
    case opk::ErrorFlag::SystemFailure:
        return ErrorFlag::RuntimeError;
    case opk::ErrorFlag::InvalidOpChain:
    case opk::ErrorFlag::InvalidData:
    case opk::ErrorFlag::FileOperationError:
    case opk::ErrorFlag::ErrorWithSrcSetup:
    case opk::ErrorFlag::ErrorWithDstSetup:
    case opk::ErrorFlag::SizeMismatch:
    case opk::ErrorFlag::ImageModelDimensionError:
    case opk::ErrorFlag::ImageDimensionError:
    case opk::ErrorFlag::TensorError:
        return ErrorFlag::InvalidPipeline;
    case opk::ErrorFlag::GenericError:
        return ErrorFlag::InternalError;
    }
    return ErrorFlag::InternalError;
}

Error mapInternalError(const opk::Error &error) {
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
    auto result = opk::Tools::loadImageFileBgra(path, outWidth, outHeight);
    if (!result) {
        return tl::unexpected(mapInternalError(result.error()));
    }
    return std::move(*result);
}

} // namespace opk::runtime
