/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "runtime/OpChain.h"

#include "mediaio/VideoFrame.h"
#include "op/OpChain.h"
#include "op/OpChainContext.h"
#include "pek/Perception.h"
#include "pek/PerceptionSerializer.h"
#include "pek/Result.h"

#include <fmt/core.h>
#include <magic_enum/magic_enum.hpp>
#include <nlohmann/json.hpp>

#include <exception>
#include <memory>
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

struct OpChain::Impl {
    pek::op::OpChain chain;
    bool loaded = false;
};

OpChain::OpChain() = default;
OpChain::~OpChain() = default;
OpChain::OpChain(OpChain &&other) noexcept = default;
OpChain &OpChain::operator=(OpChain &&other) noexcept = default;

OpChain::OpChain(std::unique_ptr<Impl> implValue) noexcept : impl(std::move(implValue)) {}

Result<OpChain> OpChain::fromJsonFile(const std::string &path) {
    auto implValue = std::make_unique<Impl>();
    auto setupResult = implValue->chain.setupFromFile(path);
    if (!setupResult) {
        return tl::make_unexpected(mapInternalError(setupResult.error()));
    }

    implValue->loaded = true;
    return OpChain(std::move(implValue));
}

Result<std::string> OpChain::run(const VideoFrame &frame, const std::string &inferElementId) {
    if (!impl || !impl->loaded) {
        return tl::make_unexpected(Error(ErrorFlag::InvalidArgument, "No OpChain has been loaded"));
    }
    if (frame.empty()) {
        return tl::make_unexpected(Error(ErrorFlag::InvalidArgument, "VideoFrame is empty"));
    }

    pek::Perception perception;
    pek::op::OpChainContext context;
    context.inferenceInfo.inferElementId = inferElementId.empty() ? "runtime" : inferElementId;
    context.perception = &perception;
    context.videoFrames["pipelineVideoFrame"] =
        std::static_pointer_cast<pek::mediaio::VideoFrame>(frame.internalFrameHandle());

    auto executeResult = impl->chain.execute(context);
    if (!executeResult) {
        return tl::make_unexpected(mapInternalError(executeResult.error()));
    }

    try {
        const nlohmann::json perceptionJson = perception;
        return perceptionJson.dump();
    } catch (const std::exception &e) {
        return tl::make_unexpected(
            Error(ErrorFlag::RuntimeError,
                  fmt::format("Failed to serialize Perception metadata: {}", e.what())));
    }
}

bool OpChain::loaded() const noexcept {
    return impl && impl->loaded;
}

} // namespace pek::runtime
