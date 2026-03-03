/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once
#include "amp/Perception.h"
#include "gst/GstMetaWrapper.h"
#include <gst/gst.h>

G_BEGIN_DECLS
GType GstMetaPerceptionContext_get_type(void);
const GstMetaInfo *GstMetaPerceptionContext_get_info(void);
G_END_DECLS

namespace amp {
struct PerceptionContextMetaTraits {
    static const char *api_name();
    static const char *meta_name();
    static const gchar **tags();
};
using GstMetaPerceptionContext = GstMetaContainer<amp::Perception>;
using PerceptionContextMeta = GstMetaWrapper<amp::Perception, PerceptionContextMetaTraits>;
} // namespace amp
