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

#pragma once

#include <array>
#include <span>
#include <string_view>

#include <gst/gst.h>

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
    static const std::span<const gchar *> tags() {
        static std::array<const gchar *, 4> t = {
            "frame-results",
            "inference",
            "metadata",
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
