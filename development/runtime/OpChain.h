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

#pragma once

#include "runtime/Result.h"
#include "runtime/VideoFrame.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace opk::runtime {

/**
 * @brief Public runtime API wrapper for executing an OPK OpChain on one video frame.
 *
 * OpChain is the non-GStreamer direct inference API. It loads an existing OPK
 * opchain JSON file, executes it against a runtime::VideoFrame, and returns the
 * serialized FrameResults metadata.
 *
 * OpChain owns the loaded opchain resources and is move-only. run() and
 * runPacket() are synchronous calls: the input VideoFrame is borrowed only until
 * the call returns, and returned strings/vectors own their storage. A single
 * OpChain instance is not documented as safe for concurrent run calls.
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
     *
     * The frame is borrowed for the duration of the call. The returned string owns
     * the serialized JSON/base64 wrapper and remains valid independently of the
     * OpChain and VideoFrame.
     *
     * @param frame Input video frame. Existing image opchains expect BGRA input.
     * @param inferElementId Stable id written into inference metadata.
     * @return Serialized FrameResults JSON wrapper on success.
     */
    Result<std::string> run(const VideoFrame &frame, const std::string &inferElementId = "runtime");

    /**
     * @brief Executes the opchain on one frame and returns the binary Perception packet.
     *
     * The frame is borrowed for the duration of the call. The returned vector owns
     * the serialized packet bytes and remains valid independently of the OpChain
     * and VideoFrame.
     *
     * @param frame Input video frame. Existing image opchains expect BGRA input.
     * @param inferElementId Stable id written into inference metadata.
     * @return Serialized Perception FrameResults packet on success.
     */
    Result<std::vector<std::uint8_t>> runPacket(const VideoFrame &frame,
                                                const std::string &inferElementId = "runtime");

    /** @brief Returns true when an opchain has been loaded. */
    bool loaded() const noexcept;

  private:
    struct Impl;

    explicit OpChain(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl;
};

} // namespace opk::runtime
