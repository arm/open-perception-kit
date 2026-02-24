#pragma once
#include <gst/gst.h>

#include "amp/Perception.h"
#include "gst/GstMetaWrapper.h"

namespace amp {

struct PerceptionContextMetaTraits {
    using Payload = Perception;

    static const char *api_name() {
        return "com_arm_amp_meta_PerceptionAPI_v1";
    }
    static const char *meta_name() {
        return "com_arm_amp_meta_Perception";
    }
    static const gchar **tags() {
        static const gchar *t[] = {
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

using PerceptionMeta = Meta<PerceptionContextMetaTraits>;

} // namespace amp
