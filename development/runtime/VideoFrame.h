/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "runtime/Result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace pek::runtime {

class OpChain;

/**
 * @brief Public runtime API wrapper for a CPU-backed video frame.
 *
 * VideoFrame is the direct-inference input type used by runtime::OpChain. It keeps
 * public headers independent from internal mediaio types while allowing
 * the implementation to pass the frame into existing preprocessing ops.
 */
class VideoFrame {
  public:
    /** @brief Constructs an empty frame. */
    VideoFrame();

    /** @brief Destroys the frame wrapper. */
    ~VideoFrame();

    VideoFrame(const VideoFrame &other) noexcept;
    VideoFrame &operator=(const VideoFrame &other) noexcept;
    VideoFrame(VideoFrame &&other) noexcept;
    VideoFrame &operator=(VideoFrame &&other) noexcept;

    /**
     * @brief Copies packed BGRA pixels from a raw memory range into an owning VideoFrame.
     * @param data Source pixel bytes in BGRA HWC order.
     * @param byteCount Number of bytes available from data.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param strideBytes Row stride in bytes, or 0 for width * 4.
     * @return VideoFrame on success, or InvalidArgument on invalid metadata.
     */
    static Result<VideoFrame> copyBgra(const std::uint8_t *data,
                                       std::size_t byteCount,
                                       std::size_t width,
                                       std::size_t height,
                                       std::size_t strideBytes = 0);

    /**
     * @brief Borrows packed BGRA pixels without copying or taking ownership.
     *
     * The caller must keep data valid and unchanged until any OpChain::run() using
     * the returned frame has completed.
     *
     * @param data Source pixel bytes in BGRA HWC order.
     * @param byteCount Number of bytes available from data.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param strideBytes Row stride in bytes, or 0 for width * 4.
     * @return VideoFrame on success, or InvalidArgument on invalid metadata.
     */
    static Result<VideoFrame> borrowBgra(const std::uint8_t *data,
                                         std::size_t byteCount,
                                         std::size_t width,
                                         std::size_t height,
                                         std::size_t strideBytes = 0);

    /**
     * @brief Borrows packed BGRA pixels from a vector without copying or taking ownership.
     *
     * The caller must keep pixels valid and unchanged until any OpChain::run() using
     * the returned frame has completed.
     *
     * @param pixels Source pixel bytes in BGRA HWC order.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param strideBytes Row stride in bytes, or 0 for width * 4.
     * @return VideoFrame on success, or InvalidArgument on invalid metadata.
     */
    static Result<VideoFrame> borrowBgra(const std::vector<std::uint8_t> &pixels,
                                         std::size_t width,
                                         std::size_t height,
                                         std::size_t strideBytes = 0);

    /**
     * @brief Copies packed BGRA pixels from a vector into an owning VideoFrame.
     * @param pixels Source pixel bytes in BGRA HWC order.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param strideBytes Row stride in bytes, or 0 for width * 4.
     * @return VideoFrame on success, or InvalidArgument on invalid metadata.
     */
    static Result<VideoFrame> copyBgra(const std::vector<std::uint8_t> &pixels,
                                       std::size_t width,
                                       std::size_t height,
                                       std::size_t strideBytes = 0);

    /**
     * @brief Moves owned packed BGRA pixels into a VideoFrame without copying.
     * @param pixels Source pixel bytes in BGRA HWC order. The vector may be moved-from on success.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param strideBytes Row stride in bytes, or 0 for width * 4.
     * @return VideoFrame on success, or InvalidArgument on invalid metadata.
     */
    static Result<VideoFrame> moveBgra(std::vector<std::uint8_t> &&pixels,
                                       std::size_t width,
                                       std::size_t height,
                                       std::size_t strideBytes = 0);

    /** @brief Returns true when this wrapper does not hold a usable frame. */
    bool empty() const noexcept;

    /** @brief Returns frame width in pixels, or 0 for an empty frame. */
    std::size_t width() const noexcept;

    /** @brief Returns frame height in pixels, or 0 for an empty frame. */
    std::size_t height() const noexcept;

    /** @brief Returns row stride in bytes, or 0 for an empty frame. */
    std::size_t strideBytes() const noexcept;

  private:
    struct Impl;

    explicit VideoFrame(std::shared_ptr<Impl> impl) noexcept;

    std::shared_ptr<void> internalFrameHandle() const noexcept;

    std::shared_ptr<Impl> impl;

    friend class OpChain;
};

} // namespace pek::runtime
