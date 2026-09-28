/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

#include "runtime/VideoFrame.h"

#include "mediaio/PixelBufferVideoFrame.h"
#include "opk/Types.h"

#include <fmt/core.h>

#include <limits>
#include <memory>
#include <utility>

namespace opk::runtime {
namespace {

constexpr std::size_t bgraBytesPerPixel = 4;

bool fitsUint32(std::size_t value) noexcept {
    return value <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
}

Result<std::size_t> normalizedBgraStride(std::size_t width, std::size_t strideBytes) {
    if (width == 0) {
        return tl::unexpected(Error(ErrorFlag::InvalidArgument, "VideoFrame width is zero"));
    }
    if (width > std::numeric_limits<std::size_t>::max() / bgraBytesPerPixel) {
        return tl::unexpected(
            Error(ErrorFlag::InvalidArgument, "VideoFrame width overflows BGRA stride"));
    }

    const std::size_t tightStride = width * bgraBytesPerPixel;
    if (strideBytes == 0) {
        return tightStride;
    }
    if (strideBytes < tightStride) {
        return tl::unexpected(
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
        return tl::unexpected(Error(ErrorFlag::InvalidArgument, "VideoFrame height is zero"));
    }
    if (!fitsUint32(width) || !fitsUint32(height) || !fitsUint32(strideBytes)) {
        return tl::unexpected(Error(ErrorFlag::InvalidArgument,
                                    "VideoFrame dimensions or stride exceed the supported range"));
    }
    if (height > std::numeric_limits<std::size_t>::max() / strideBytes) {
        return tl::unexpected(
            Error(ErrorFlag::InvalidArgument, "VideoFrame byte size calculation overflow"));
    }

    const std::size_t requiredBytes = height * strideBytes;
    if (byteCount < requiredBytes) {
        return tl::unexpected(
            Error(ErrorFlag::InvalidArgument,
                  fmt::format("VideoFrame BGRA buffer has {} bytes, but {} are required",
                              byteCount,
                              requiredBytes)));
    }

    return {};
}

} // namespace

struct VideoFrame::Impl {
    std::shared_ptr<opk::mediaio::VideoFrame> frame;
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
        return tl::unexpected(Error(ErrorFlag::InvalidArgument, "VideoFrame source data is null"));
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
        return tl::unexpected(Error(ErrorFlag::InvalidArgument, "VideoFrame source data is null"));
    }

    auto normalizedStride = normalizedBgraStride(width, strideBytes);
    if (!normalizedStride) {
        return tl::unexpected(std::move(normalizedStride.error()));
    }

    auto validBuffer = validateBgraBuffer(byteCount, width, height, *normalizedStride);
    if (!validBuffer) {
        return tl::unexpected(std::move(validBuffer.error()));
    }

    auto internalFrame = opk::mediaio::makeReadOnlyPixelBufferVideoFrame(
        data,
        byteCount,
        static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height),
        opk::RawImagePixelFormat::Bgra,
        static_cast<std::uint32_t>(*normalizedStride));

    if (!internalFrame) {
        return tl::unexpected(
            Error(ErrorFlag::InternalError, "Failed to create borrowed pixel-buffer VideoFrame"));
    }

    auto implValue = std::make_shared<Impl>();
    implValue->frame = std::shared_ptr<opk::mediaio::VideoFrame>(std::move(internalFrame));
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
        return tl::unexpected(std::move(normalizedStride.error()));
    }

    auto validBuffer = validateBgraBuffer(pixels.size(), width, height, *normalizedStride);
    if (!validBuffer) {
        return tl::unexpected(std::move(validBuffer.error()));
    }

    auto internalFrame =
        opk::mediaio::makeOwnedPixelBufferVideoFrame(std::move(pixels),
                                                     static_cast<std::uint32_t>(width),
                                                     static_cast<std::uint32_t>(height),
                                                     opk::RawImagePixelFormat::Bgra,
                                                     static_cast<std::uint32_t>(*normalizedStride),
                                                     opk::AccessMode::Read);

    if (!internalFrame) {
        return tl::unexpected(
            Error(ErrorFlag::InternalError, "Failed to create internal pixel-buffer VideoFrame"));
    }

    auto implValue = std::make_shared<Impl>();
    implValue->frame = std::shared_ptr<opk::mediaio::VideoFrame>(std::move(internalFrame));
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

} // namespace opk::runtime
