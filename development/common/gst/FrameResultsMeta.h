/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

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
