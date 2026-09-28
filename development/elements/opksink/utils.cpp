/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gst/gst.h>
#include <gst/gstpipeline.h>

#include "Log.h"
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
    // GST_IS_ELEMENT() is a macro performing a type check with no side effects
    if (!top_level || !GST_IS_ELEMENT(top_level)) { // NOSONAR
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
    // GST_IS_ELEMENT() is a macro performing a type check with no side effects
    if (!top_level || !GST_IS_ELEMENT(top_level) || type_name.empty()) { // NOSONAR
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
        opk::log::debug("No pad iterator");
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

                opk::log::debug("Sink pad: {} (template: {})", GST_PAD_NAME(pad), templ_name);
            }
            g_value_reset(&item);
            break;
        }
        case GST_ITERATOR_RESYNC:
            gst_iterator_resync(it);
            break;
        case GST_ITERATOR_ERROR:
            opk::log::debug("Pad iteration error");
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
void release_request_pad_and_unref(GstElement *elem, GstPad **ppad) noexcept {
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

    // GST_IS_ELEMENT() and GST_IS_PAD() are macros performing a type checks with no side effects
    if (elem && GST_IS_ELEMENT(elem) && GST_IS_PAD(*ppad)) { // NOSONAR
        gst_element_remove_pad(elem, *ppad);                 // drops element’s ref
    } else {
        gst_object_unref(*ppad); // fallback if somehow not parented
    }
    *ppad = nullptr;
}

bool set_state_elements_many(GstState state, std::initializer_list<GstElement *> elems) noexcept {
    bool ok = true;
    for (GstElement *e : elems) {
        if (!e)
            continue;

        const auto r = gst_element_set_state(e, state);
        // For teardown, ASYNC is usually fine; FAILURE is not.
        if (r == GST_STATE_CHANGE_FAILURE) {
            ok = false;
            g_debug("Failed to set state on %s", GST_ELEMENT_NAME(e));
        }
    }

    return ok;
}
