#include "GstTools.h"

GstElement* GstTools::getOverlayElement(GstVideoFilter* videoFilter, const char* overlayElementName) {

    GstObject* parent_obj = gst_element_get_parent(GST_ELEMENT(videoFilter));
    if (parent_obj) {
      if (GST_IS_ELEMENT(parent_obj)) {
        GstElement *parent_elem = GST_ELEMENT_CAST(parent_obj);
        GstElement *overlay = gst_bin_get_by_name(GST_BIN(parent_elem), overlayElementName);
        if (overlay) return overlay;
      }
    }

    return nullptr;
}

