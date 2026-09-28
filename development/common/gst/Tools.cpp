/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

#include "Tools.h"

using namespace gst;

GstElement *Tools::getOverlayElement(GstVideoFilter *videoFilter) {
    if (!videoFilter)
        return nullptr;

    // Get the parent (should be a bin)
    GstObject *parent = gst_element_get_parent(GST_ELEMENT(videoFilter));
    // GST_IS_BIN() is a macro performing a type check with no side effects
    if (!parent || !GST_IS_BIN(parent)) { // NOSONAR
        if (parent)
            gst_object_unref(parent);
        return nullptr;
    }

    // Iterate over elements in the parent bin
    GstIterator *it = gst_bin_iterate_elements(GST_BIN(parent));
    gst_object_unref(parent); // we don't need the parent anymore

    GstElement *result = nullptr;
    GValue item = G_VALUE_INIT;
    gboolean done = FALSE;

    while (!done) {
        switch (gst_iterator_next(it, &item)) {
        case GST_ITERATOR_OK: {
            GstElement *e = GST_ELEMENT(g_value_get_object(&item));
            GstElementFactory *f = gst_element_get_factory(e);
            if (f) {
                const gchar *fname = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(f));
                if (g_strcmp0(fname, "textoverlay") == 0 ||
                    g_strcmp0(fname, "subtitleoverlay") == 0) {
                    result = GST_ELEMENT(gst_object_ref(e)); // take a ref for caller
                    g_value_unset(&item);
                    done = TRUE;
                    break;
                }
            }
            g_value_unset(&item);
            break;
        }
        case GST_ITERATOR_RESYNC:
            gst_iterator_resync(it);
            break;
        case GST_ITERATOR_ERROR:
        case GST_ITERATOR_DONE:
            done = TRUE;
            break;
        }
    }

    gst_iterator_free(it);
    return result; // nullptr if not found
}
