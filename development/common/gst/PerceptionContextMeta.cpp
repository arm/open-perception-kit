#include "GstMetaWrapper.cpp"

namespace amp {
const char *PerceptionContextMetaTraits::api_name() {
    return "GstMetaPerceptionContextAPI";
}

const char *PerceptionContextMetaTraits::meta_name() {
    return "GstMetaPerceptionContext";
}

const gchar **PerceptionContextMetaTraits::tags() {
    static const gchar *tags[] = {"perception", "inference", "detections", NULL};
    return tags;
}

// Explicit instantiation for PerceptionContext
template class GstMetaWrapper<amp::Perception, amp::PerceptionContextMetaTraits>;
} // namespace amp

extern "C" {
GType GstMetaPerceptionContext_get_type(void) {
    return amp::PerceptionContextMeta::get_type();
}

const GstMetaInfo *GstMetaPerceptionContext_get_info(void) {
    return amp::PerceptionContextMeta::get_info();
}
} // extern "C"
