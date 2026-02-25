/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include <gst/gst.h>
#include <gst/gstpipeline.h>

#include "auxiliary.h"
#include "gst/gstbin.h"
#include "gst/gstelement.h"
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

GstElement *get_element_by_name(GstElement *top_level, const std::string &element_name) {
    if (!top_level || !GST_IS_ELEMENT(top_level)) {
        return nullptr;
    }

    // Fast path: direct name match
    const gchar *name = gst_element_get_name(top_level);
    if (name && element_name == name) {
        return GST_ELEMENT(gst_object_ref(top_level));
    }

    // Only bins can contain other elements
    if (!GST_IS_BIN(top_level)) {
        return nullptr;
    }

    GstBin *bin = GST_BIN(top_level);
    GstIterator *it = gst_bin_iterate_elements(bin);
    GValue item = G_VALUE_INIT;
    GstElement *found = nullptr;

    while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
        GstElement *child = GST_ELEMENT(g_value_get_object(&item));

        // Recurse
        found = get_element_by_name(child, element_name);

        g_value_reset(&item);

        if (found) {
            break;
        }
    }

    gst_iterator_free(it);
    return found;
}

GstElement *get_element_by_type(GstElement *top_level, const std::string &type_name) {
    if (!top_level || !GST_IS_ELEMENT(top_level) || type_name.empty()) {
        return nullptr;
    }

    /* Check this element's factory (type) */
    GstElementFactory *factory = gst_element_get_factory(top_level);
    if (factory) {
        const gchar *factory_name = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
        if (factory_name && type_name == factory_name) {
            /* element matches: ref and return */
            return GST_ELEMENT(gst_object_ref(top_level));
        }
    }

    /* If not a bin, can't have children */
    if (!GST_IS_BIN(top_level)) {
        return nullptr;
    }

    /* Iterate children and recurse */
    GstBin *bin = GST_BIN(top_level);
    GstIterator *it = gst_bin_iterate_elements(bin);
    GValue val = G_VALUE_INIT;
    GstElement *found = nullptr;

    while (gst_iterator_next(it, &val) == GST_ITERATOR_OK) {
        GstElement *child = GST_ELEMENT(g_value_get_object(&val));
        /* recurse */
        found = get_element_by_type(child, type_name);
        g_value_reset(&val);

        if (found) {
            break;
        }
    }

    gst_iterator_free(it);
    return found;
}

void dump_sink_pads(GstElement *element) {
#ifndef NDEBUG
    g_return_if_fail(GST_IS_ELEMENT(element));

    GstIterator *it = gst_element_iterate_pads(element);
    if (!it) {
        DBG("No pad iterator");
        return;
    }

    GValue item = G_VALUE_INIT;
    gboolean done = FALSE;

    while (!done) {
        switch (gst_iterator_next(it, &item)) {
        case GST_ITERATOR_OK: {
            GstPad *pad = GST_PAD(g_value_get_object(&item));
            if (pad && gst_pad_get_direction(pad) == GST_PAD_SINK) {
                const GstPadTemplate *templ = gst_pad_get_pad_template(pad);
                const gchar *templ_name =
                    templ ? GST_PAD_TEMPLATE_NAME_TEMPLATE(templ) : "(no template)";

                DBG("Sink pad: {} (template: {})", GST_PAD_NAME(pad), templ_name);
            }
            g_value_reset(&item);
            break;
        }
        case GST_ITERATOR_RESYNC:
            gst_iterator_resync(it);
            break;
        case GST_ITERATOR_ERROR:
            DBG("Pad iteration error");
            done = TRUE;
            break;
        case GST_ITERATOR_DONE:
            done = TRUE;
            break;
        }
    }

    g_value_unset(&item);
    gst_iterator_free(it);
#endif // !NDEBUG
}

void dump_pipeline_graph(GstElement *element, const std::string &file_name) {

#ifndef NDEBUG
    GstElement *pipeline = get_top_pipeline(GST_ELEMENT(element));
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(
        GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, file_name.c_str());
    gst_object_unref(pipeline);
#endif // !NDEBUG
}
void release_request_pad_and_unref(GstElement *elem, GstPad **ppad) {
    if (!ppad || !*ppad)
        return;

    if (elem) {
        GstObject *parent = gst_object_get_parent(GST_OBJECT(*ppad));
        if (parent == GST_OBJECT(elem)) {
            gst_element_release_request_pad(elem, *ppad);
        }
        if (parent)
            gst_object_unref(parent);
    }

    gst_object_unref(*ppad);
    *ppad = nullptr;
}

void remove_pad_if_present(GstElement *elem, GstPad **ppad) {
    if (!ppad || !*ppad)
        return;
    if (elem && GST_IS_ELEMENT(elem) && GST_IS_PAD(*ppad)) {
        gst_element_remove_pad(elem, *ppad); // drops element’s ref
    } else {
        gst_object_unref(*ppad); // fallback if somehow not parented
    }
    *ppad = nullptr;
}

bool set_state_elements_many(GstState state, std::initializer_list<GstElement *> elems) {
    bool ok = true;
    for (GstElement *e : elems) {
        if (!e)
            continue;

        const auto r = gst_element_set_state(e, state);
        // For teardown, ASYNC is usually fine; FAILURE is not.
        if (r == GST_STATE_CHANGE_FAILURE) {
            ok = false;
            DBG("Failed to set state on {}", GST_ELEMENT_NAME(e));
        }
    }

    return ok;
}
