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

#include "FrameResultsPacket.h"

#include <stdexcept>
#include <utility>

namespace opk {

FrameResultsPacket::FrameResultsPacket(
    std::shared_ptr<const open_perception_kit::FrameResults> results)
    : m_results(std::move(results)) {
    if (!m_results)
        throw std::invalid_argument("Missing FrameResults snapshot");
}

std::shared_ptr<GBytes> FrameResultsPacket::bytes() const {
    std::call_once(m_once, [this] {
        auto packet = std::make_unique<flatbuffers::DetachedBuffer>(m_results->serialize());
        auto *const bytes = g_bytes_new_with_free_func(
            packet->data(),
            packet->size(),
            [](gpointer data) { delete static_cast<flatbuffers::DetachedBuffer *>(data); },
            packet.get());
        packet.release(); // GBytes now owns the serializer's allocation.
        m_bytes = std::shared_ptr<GBytes>(bytes, g_bytes_unref);
        // The packet is independent of the source; retain the source only when
        // serialization fails and call_once may need to retry.
        m_results.reset();
    });
    return m_bytes;
}

std::span<const std::uint8_t> FrameResultsPacket::view() const {
    const auto packet = bytes();
    return {static_cast<const std::uint8_t *>(g_bytes_get_data(packet.get(), nullptr)),
            g_bytes_get_size(packet.get())};
}

} // namespace opk
