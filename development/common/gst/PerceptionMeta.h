/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once
#include <string_view>
#include <array>
#include <span>

#include <gst/gst.h>

#include "amp/Perception.h"
#include "gst/GstMetaWrapper.h"

namespace amp {

struct PerceptionMetaTraits {
    using Payload = Perception;

    static const std::string_view api_name() {
        return "com_arm_amp_meta_PerceptionAPI_v1";
    }
    static const std::string_view meta_name() {
        return "com_arm_amp_meta_Perception";
    }
    static const std::span<const gchar *> tags() {
        static std::array<const gchar *, 4> t = {
            "perception",
            "inference",
            "detections",
            nullptr,
        };

        return t;
    }

    static Payload clone(const Payload &p) {
        return p;
    }
};

using PerceptionMeta = Meta<PerceptionMetaTraits>;

} // namespace amp
