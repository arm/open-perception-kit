/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

#include <cstdint>
#include <glib.h>
#include <memory>
#include <mutex>
#include <span>

#include "opk/FrameResults.h"

namespace opk {

// A lazy packet for one immutable metadata snapshot; never owns image memory.
// FrameResultsMeta::mutate detaches this snapshot before writing to the payload.
class FrameResultsPacket {
  public:
    explicit FrameResultsPacket(std::shared_ptr<const open_perception_kit::FrameResults> results);

    // Returns shared immutable bytes. Failed serialization is not cached.
    [[nodiscard]] std::shared_ptr<GBytes> bytes() const;

    // Borrowed, read-only bytes with the same lazy initialization as bytes().
    // Keep this FrameResultsPacket alive while using the view.
    [[nodiscard]] std::span<const std::uint8_t> view() const;

  private:
    mutable std::shared_ptr<const open_perception_kit::FrameResults> m_results;
    mutable std::once_flag m_once;
    mutable std::shared_ptr<GBytes> m_bytes;
};

} // namespace opk
