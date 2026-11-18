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

uint64_t
OpChainContext::videoFrameId(const std::string &name,
                             const open_perception_kit::metadata::ProducerInfoT &producer) {
    if (!frameResults)
        return 0;
    const auto source = videoFrames.find(name);
    if (source == videoFrames.end() || !source->second)
        return 0;
    const auto &frame = source->second;
    const auto found = videoFrameIds.find(frame);
    if (found != videoFrameIds.end())
        return found->second;
    open_perception_kit::metadata::FrameContextT payload;
    const auto layerId = "rootLayer_" + inferenceInfo.inferElementId;
    payload.layer =
        open_perception_kit::makeLayerInfo({.inferElementId = layerId, .producer = &producer});
    payload.video = std::make_unique<open_perception_kit::metadata::VideoFrameContextT>();
    payload.video->object = open_perception_kit::makeObjectMeta();
    payload.video->original_width = frame->width();
    payload.video->original_height = frame->height();
    if (frame->timestampNs() != opk::mediaio::InvalidTimestampNs)
        payload.video->presentation_ts_ns = frame->timestampNs();
    const auto id = payload.video->object->id;
    frameResults->add(std::move(payload));
    videoFrameIds.emplace(frame, id);
    return id;
}

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
