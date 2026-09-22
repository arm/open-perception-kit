/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "runtime/OpChain.h"

#include "mediaio/VideoFrame.h"
#include "op/OpChain.h"
#include "op/OpChainContext.h"
#include "opk/Base64.h"
#include "opk/FrameResults.h"
#include "opk/Result.h"

#include <fmt/core.h>
#include <magic_enum/magic_enum.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <utility>
#include <vector>

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

std::string serializePacketJson(std::span<const std::uint8_t> packet) {
    nlohmann::json wrapper;
    wrapper["frame_results_encoding"] = "perception-frame-results+base64";
    wrapper["frame_results_packet_b64"] = opk::base64Encode(packet);
    return wrapper.dump();
}

} // namespace

struct OpChain::Impl {
    opk::op::OpChain chain;
    bool loaded = false;

    Result<perception::FrameResults> runFrameResults(const VideoFrame &frame,
                                                     const std::string &inferElementId);
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
        return tl::unexpected(mapInternalError(setupResult.error()));
    }

    implValue->loaded = true;
    return OpChain(std::move(implValue));
}

Result<perception::FrameResults> OpChain::Impl::runFrameResults(const VideoFrame &frame,
                                                                const std::string &inferElementId) {
    if (!loaded) {
        return tl::unexpected(Error(ErrorFlag::InvalidArgument, "No OpChain has been loaded"));
    }
    if (frame.empty()) {
        return tl::unexpected(Error(ErrorFlag::InvalidArgument, "VideoFrame is empty"));
    }

    perception::FrameResults frameResults;
    opk::op::OpChainContext context;
    context.inferenceInfo.inferElementId = inferElementId.empty() ? "runtime" : inferElementId;
    context.frameResults = &frameResults;
    context.videoFrames["pipelineVideoFrame"] =
        std::static_pointer_cast<opk::mediaio::VideoFrame>(frame.internalFrameHandle());

    auto executeResult = chain.execute(context);
    if (!executeResult) {
        return tl::unexpected(mapInternalError(executeResult.error()));
    }

    return frameResults;
}

Result<std::string> OpChain::run(const VideoFrame &frame, const std::string &inferElementId) {
    auto packet = runPacket(frame, inferElementId);
    if (!packet) {
        return tl::unexpected(std::move(packet.error()));
    }

    try {
        return serializePacketJson(*packet);
    } catch (const std::exception &e) {
        return tl::unexpected(
            Error(ErrorFlag::RuntimeError,
                  fmt::format("Failed to serialize FrameResults transport wrapper: {}", e.what())));
    }
}

Result<std::vector<std::uint8_t>> OpChain::runPacket(const VideoFrame &frame,
                                                     const std::string &inferElementId) {
    if (!impl) {
        return tl::unexpected(Error(ErrorFlag::InvalidArgument, "No OpChain has been loaded"));
    }

    auto frameResults = impl->runFrameResults(frame, inferElementId);
    if (!frameResults) {
        return tl::unexpected(std::move(frameResults.error()));
    }

    try {
        return perception::serialize(*frameResults);
    } catch (const std::exception &e) {
        return tl::unexpected(
            Error(ErrorFlag::RuntimeError,
                  fmt::format("Failed to serialize FrameResults metadata: {}", e.what())));
    }
}

bool OpChain::loaded() const noexcept {
    return impl && impl->loaded;
}

} // namespace opk::runtime
