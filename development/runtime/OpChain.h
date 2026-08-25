/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "runtime/Result.h"
#include "runtime/VideoFrame.h"

#include <memory>
#include <string>

namespace pek::runtime {

/**
 * @brief Public runtime API wrapper for executing a PEK OpChain on one video frame.
 *
 * OpChain is the non-GStreamer direct inference API. It loads an existing PEK
 * opchain JSON file, executes it against a runtime::VideoFrame, and returns the
 * serialized FrameResults JSON wrapper.
 */
class OpChain {
  public:
    /** @brief Constructs an empty wrapper. */
    OpChain();

    /** @brief Releases the loaded opchain and its runtime resources. */
    ~OpChain();

    OpChain(const OpChain &) = delete;
    OpChain &operator=(const OpChain &) = delete;

    OpChain(OpChain &&other) noexcept;
    OpChain &operator=(OpChain &&other) noexcept;

    /**
     * @brief Loads an OpChain from a JSON descriptor file.
     * @param path Path to an opchain JSON file.
     * @return Loaded OpChain on success, or a runtime error on failure.
     */
    static Result<OpChain> fromJsonFile(const std::string &path);

    /**
     * @brief Executes the opchain on one frame and returns a FrameResults JSON wrapper.
     * @param frame Input video frame. Existing image opchains expect BGRA input.
     * @param inferElementId Stable id written into inference metadata.
     * @return Serialized FrameResults JSON wrapper on success.
     */
    Result<std::string> run(const VideoFrame &frame, const std::string &inferElementId = "runtime");

    /** @brief Returns true when an opchain has been loaded. */
    bool loaded() const noexcept;

  private:
    struct Impl;

    explicit OpChain(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl;
};

} // namespace pek::runtime
