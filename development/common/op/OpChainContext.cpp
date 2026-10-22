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

#include "op/OpChainContext.h"

#include <algorithm>

using namespace opk::op;

const opk::mediaio::VideoFrame *OpChainContext::getReadableVideoFrame(const std::string &name) {
    const auto found{videoFrames.find(name)};
    if (found == videoFrames.end() || !found->second) {
        return nullptr;
    }
    const auto &frame{found->second};
    const auto readable{[](const opk::mediaio::VideoFrame &candidate) {
        const auto planes{candidate.planes()};
        return !planes.empty() && std::ranges::all_of(planes, [](const auto &plane) {
            return plane.hasHostData() && plane.canRead();
        });
    }};
    if (readable(*frame)) {
        return frame.get();
    }
    auto &mapped{readableVideoFrames[frame]};
    if (!mapped) {
        mapped = frame->map(opk::AccessMode::Read);
    }
    return mapped && readable(*mapped) ? mapped.get() : nullptr;
}
