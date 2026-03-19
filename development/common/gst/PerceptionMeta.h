/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once
#include <vector>

#include <gst/gst.h>

#include "amp/Perception.h"
#include "gst/GstMetaWrapper.h"

namespace amp {

struct PerceptionMetaTraits {
    using Payload = Perception;

    static const char *api_name() {
        return "com_arm_amp_meta_PerceptionAPI_v1";
    }
    static const char *meta_name() {
        return "com_arm_amp_meta_Perception";
    }
    static const gchar **tags() {
        static std::vector<const gchar *> t = {
            "perception",
            "inference",
            "detections",
            nullptr,
        };

        return t.data();
    }

    static Payload clone(const Payload &p) {
        return p;
    }
};

using PerceptionMeta = Meta<PerceptionMetaTraits>;

} // namespace amp
