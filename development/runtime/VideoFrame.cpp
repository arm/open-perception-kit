/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "runtime/VideoFrame.h"

#include "mediaio/PixelBufferVideoFrame.h"
#include "pek/Types.h"

#include <fmt/core.h>

#include <limits>
#include <memory>
#include <utility>

namespace pek::runtime {
namespace {

constexpr std::size_t bgraBytesPerPixel = 4;

bool fitsUint32(std::size_t value) noexcept {
    return value <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
}

Result<std::size_t> normalizedBgraStride(std::size_t width, std::size_t strideBytes) {
    if (width == 0) {
        return tl::make_unexpected(Error(ErrorFlag::InvalidArgument, "VideoFrame width is zero"));
    }
    if (width > std::numeric_limits<std::size_t>::max() / bgraBytesPerPixel) {
        return tl::make_unexpected(
            Error(ErrorFlag::InvalidArgument, "VideoFrame width overflows BGRA stride"));
    }

    const std::size_t tightStride = width * bgraBytesPerPixel;
    if (strideBytes == 0) {
        return tightStride;
    }
    if (strideBytes < tightStride) {
        return tl::make_unexpected(
            Error(ErrorFlag::InvalidArgument,
                  fmt::format("VideoFrame BGRA stride {} is smaller than tight stride {}",
                              strideBytes,
                              tightStride)));
    }
    return strideBytes;
}

Result<void> validateBgraBuffer(std::size_t byteCount,
                                std::size_t width,
                                std::size_t height,
                                std::size_t strideBytes) {
    if (height == 0) {
        return tl::make_unexpected(Error(ErrorFlag::InvalidArgument, "VideoFrame height is zero"));
    }
    if (!fitsUint32(width) || !fitsUint32(height) || !fitsUint32(strideBytes)) {
        return tl::make_unexpected(
            Error(ErrorFlag::InvalidArgument,
                  "VideoFrame dimensions or stride exceed the supported range"));
    }
    if (height > std::numeric_limits<std::size_t>::max() / strideBytes) {
        return tl::make_unexpected(
            Error(ErrorFlag::InvalidArgument, "VideoFrame byte size calculation overflow"));
    }

    const std::size_t requiredBytes = height * strideBytes;
    if (byteCount < requiredBytes) {
        return tl::make_unexpected(
            Error(ErrorFlag::InvalidArgument,
                  fmt::format("VideoFrame BGRA buffer has {} bytes, but {} are required",
                              byteCount,
                              requiredBytes)));
    }

    return {};
}

} // namespace

struct VideoFrame::Impl {
    std::shared_ptr<pek::mediaio::VideoFrame> frame;
    std::size_t frameWidth = 0;
    std::size_t frameHeight = 0;
    std::size_t frameStrideBytes = 0;
};

VideoFrame::VideoFrame() = default;
VideoFrame::~VideoFrame() = default;
VideoFrame::VideoFrame(const VideoFrame &other) noexcept = default;
VideoFrame &VideoFrame::operator=(const VideoFrame &other) noexcept = default;
VideoFrame::VideoFrame(VideoFrame &&other) noexcept = default;
VideoFrame &VideoFrame::operator=(VideoFrame &&other) noexcept = default;

VideoFrame::VideoFrame(std::shared_ptr<Impl> implValue) noexcept : impl(std::move(implValue)) {}

std::shared_ptr<void> VideoFrame::internalFrameHandle() const noexcept {
    return impl ? impl->frame : nullptr;
}

Result<VideoFrame> VideoFrame::copyBgra(const std::uint8_t *data,
                                        std::size_t byteCount,
                                        std::size_t width,
                                        std::size_t height,
                                        std::size_t strideBytes) {
    if (data == nullptr) {
        return tl::make_unexpected(
            Error(ErrorFlag::InvalidArgument, "VideoFrame source data is null"));
    }

    std::vector<std::uint8_t> pixels(data, data + byteCount);
    return moveBgra(std::move(pixels), width, height, strideBytes);
}

Result<VideoFrame> VideoFrame::copyBgra(const std::vector<std::uint8_t> &pixels,
                                        std::size_t width,
                                        std::size_t height,
                                        std::size_t strideBytes) {
    return moveBgra(
        std::vector<std::uint8_t>(pixels.begin(), pixels.end()), width, height, strideBytes);
}

Result<VideoFrame> VideoFrame::borrowBgra(const std::uint8_t *data,
                                          std::size_t byteCount,
                                          std::size_t width,
                                          std::size_t height,
                                          std::size_t strideBytes) {
    if (data == nullptr) {
        return tl::make_unexpected(
            Error(ErrorFlag::InvalidArgument, "VideoFrame source data is null"));
    }

    auto normalizedStride = normalizedBgraStride(width, strideBytes);
    if (!normalizedStride) {
        return tl::make_unexpected(std::move(normalizedStride.error()));
    }

    auto validBuffer = validateBgraBuffer(byteCount, width, height, *normalizedStride);
    if (!validBuffer) {
        return tl::make_unexpected(std::move(validBuffer.error()));
    }

    auto internalFrame = pek::mediaio::makeReadOnlyPixelBufferVideoFrame(
        data,
        byteCount,
        static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height),
        pek::RawImagePixelFormat::Bgra,
        static_cast<std::uint32_t>(*normalizedStride));

    if (!internalFrame) {
        return tl::make_unexpected(
            Error(ErrorFlag::InternalError, "Failed to create borrowed pixel-buffer VideoFrame"));
    }

    auto implValue = std::make_shared<Impl>();
    implValue->frame = std::shared_ptr<pek::mediaio::VideoFrame>(std::move(internalFrame));
    implValue->frameWidth = width;
    implValue->frameHeight = height;
    implValue->frameStrideBytes = *normalizedStride;
    return VideoFrame(std::move(implValue));
}

Result<VideoFrame> VideoFrame::borrowBgra(const std::vector<std::uint8_t> &pixels,
                                          std::size_t width,
                                          std::size_t height,
                                          std::size_t strideBytes) {
    return borrowBgra(pixels.data(), pixels.size(), width, height, strideBytes);
}

Result<VideoFrame> VideoFrame::moveBgra(std::vector<std::uint8_t> &&pixels,
                                        std::size_t width,
                                        std::size_t height,
                                        std::size_t strideBytes) {
    auto normalizedStride = normalizedBgraStride(width, strideBytes);
    if (!normalizedStride) {
        return tl::make_unexpected(std::move(normalizedStride.error()));
    }

    auto validBuffer = validateBgraBuffer(pixels.size(), width, height, *normalizedStride);
    if (!validBuffer) {
        return tl::make_unexpected(std::move(validBuffer.error()));
    }

    auto internalFrame =
        pek::mediaio::makeOwnedPixelBufferVideoFrame(std::move(pixels),
                                                     static_cast<std::uint32_t>(width),
                                                     static_cast<std::uint32_t>(height),
                                                     pek::RawImagePixelFormat::Bgra,
                                                     static_cast<std::uint32_t>(*normalizedStride),
                                                     pek::AccessMode::Read);

    if (!internalFrame) {
        return tl::make_unexpected(
            Error(ErrorFlag::InternalError, "Failed to create internal pixel-buffer VideoFrame"));
    }

    auto implValue = std::make_shared<Impl>();
    implValue->frame = std::shared_ptr<pek::mediaio::VideoFrame>(std::move(internalFrame));
    implValue->frameWidth = width;
    implValue->frameHeight = height;
    implValue->frameStrideBytes = *normalizedStride;
    return VideoFrame(std::move(implValue));
}

bool VideoFrame::empty() const noexcept {
    return !impl || !impl->frame;
}

std::size_t VideoFrame::width() const noexcept {
    return impl ? impl->frameWidth : 0;
}

std::size_t VideoFrame::height() const noexcept {
    return impl ? impl->frameHeight : 0;
}

std::size_t VideoFrame::strideBytes() const noexcept {
    return impl ? impl->frameStrideBytes : 0;
}

} // namespace pek::runtime
