/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <array>
#include <span>
#include <string_view>

#include <gst/gst.h>

#include "gst/GstMetaWrapper.h"
#include "pek/FrameResults.h"

namespace pek {

struct FrameResultsMetaTraits {
    using Payload = perception::FrameResults;

    static const std::string_view api_name() {
        return "com_arm_pek_meta_FrameResultsAPI_v1";
    }
    static const std::string_view meta_name() {
        return "com_arm_pek_meta_FrameResults";
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

} // namespace pek
