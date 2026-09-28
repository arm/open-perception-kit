/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>

namespace gst {

struct Tools {

    static GstElement *getOverlayElement(GstVideoFilter *videoFilter);

    static void releaseElement(GstElement *element) {
        gst_object_unref(element);
    }
};
} // namespace gst
