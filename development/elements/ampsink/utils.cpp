#include <gst/gstpipeline.h>

#include "utils.h"

GstElement *get_top_pipeline(GstElement *elem) {
    if (!elem)
        return nullptr;

    GstElement *cur = elem;
    GstElement *parent = GST_ELEMENT(gst_element_get_parent(cur)); // ref
    while (parent) {
        if (GST_IS_PIPELINE(parent)) {
            return parent; // return with ref held
        }
        // move up
        GstElement *next = GST_ELEMENT(gst_element_get_parent(parent)); // ref
        gst_object_unref(parent);                                       // drop current parent ref
        parent = next;
    }
    return nullptr;
}
