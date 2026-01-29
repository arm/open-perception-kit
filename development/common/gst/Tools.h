#pragma once

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>

namespace gst {

struct Tools {

    static GstElement *getOverlayElement(GstVideoFilter *videoFilter);
    static GstElement *getOverlayElement(GstVideoFilter *videoFilter,
                                         const char *overlayElementName);

    static void releaseElement(GstElement *element) {
        gst_object_unref(element);
    }
};
} // namespace gst
