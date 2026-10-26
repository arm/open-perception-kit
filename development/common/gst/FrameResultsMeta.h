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

#include <array>
#include <span>
#include <string_view>

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gst/GstMetaWrapper.h"
#include "opk/FrameResults.h"

namespace opk {

struct FrameResultsMetaTraits {
    using Payload = open_perception_kit::FrameResults;

    static const std::string_view api_name() {
        return "com_arm_opk_meta_FrameResultsAPI_v1";
    }
    static const std::string_view meta_name() {
        return "com_arm_opk_meta_FrameResults";
    }
    /**
     * Keep "GST_META_TAG_VIDEO_STR" as the only tag, followed by nullptr. GStreamer
     * allows custom  tags, but the encoder and RTP metadata-copy filters used
     * by opksink do not accept them. Adding tags such as "inference" or "frame-results"
     * would cause the metadata to be dropped before it can be matched to an RTP
     * timestamp. Using only built in GStreamer tags keeps FrameResultsMeta attached
     * through video encoding and RTP packetization.
     */
    static const std::span<const gchar *> tags() {
        static std::array<const gchar *, 2> t = {
            GST_META_TAG_VIDEO_STR,
            nullptr,
        };

        return t;
    }

    static Payload clone(const Payload &p) {
        return p;
    }
};

using FrameResultsMeta = Meta<FrameResultsMetaTraits>;

} // namespace opk
