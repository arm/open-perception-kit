#pragma once

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>

struct GstTools {

    static GstElement* getOverlayElement(GstVideoFilter* videoFilter, const char* overlayElementName = "");
    
    static void releaseElement(GstElement* element) {
            gst_object_unref(element);
    }

};


