/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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
#include "mediaio/GstVideoFrame.h"

using namespace gst;

GstCaps *Tools::transformVideoCaps(const GstPadDirection direction,
                                   GstCaps *const caps,
                                   GstCaps *const filter,
                                   const bool copyPixels) {
    GstCaps *result = gst_caps_new_empty();
    for (guint i = 0; i < gst_caps_get_size(caps); ++i) {
        const auto *const features = gst_caps_get_features(caps, i);
        const bool theCapHasSomeSpecificFeatureRequirements =
            !gst_caps_features_is_any(features); // If "any" feature is supported it means that the
                                                 // cap supports System memory implicitly
        const bool theCapRequiresSomethingOtherThanSystemMemory =
            !gst_caps_features_is_equal(features, GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY);
        if (copyPixels && direction == GST_PAD_SRC && theCapHasSomeSpecificFeatureRequirements &&
            theCapRequiresSomethingOtherThanSystemMemory) {
            continue;
        }
        gst_caps_append_structure_full(result,
                                       gst_structure_copy(gst_caps_get_structure(caps, i)),
                                       copyPixels ? gst_caps_features_new_empty()
                                                  : gst_caps_features_copy(features));
        if (copyPixels && direction == GST_PAD_SRC) {
            gst_caps_append_structure_full(
                result,
                gst_structure_copy(gst_caps_get_structure(caps, i)),
                gst_caps_features_new(GST_CAPS_FEATURE_MEMORY_DMABUF, nullptr));
        }
    }
    if (filter != nullptr) {
        GstCaps *const intersection =
            gst_caps_intersect_full(filter, result, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(result);
        result = intersection;
    }
    return result;
}

GstFlowReturn Tools::prepareVideoBuffer(GstBaseTransform *const transform,
                                        GstBuffer *const input,
                                        GstBuffer **const output,
                                        const bool copyPixels,
                                        const bool writePixels) {
    *output = nullptr;
    GstCaps *caps = gst_pad_get_current_caps(GST_BASE_TRANSFORM_SINK_PAD(transform));
    const bool requiresDmaBuf =
        caps != nullptr &&
        gst_caps_features_contains(gst_caps_get_features(caps, 0), GST_CAPS_FEATURE_MEMORY_DMABUF);
    gst_clear_caps(&caps);
    if (requiresDmaBuf) {
        bool valid = gst_buffer_n_memory(input) > 0;
        for (guint i = 0; i < gst_buffer_n_memory(input); ++i) {
            valid = valid && gst_is_dmabuf_memory(gst_buffer_peek_memory(input, i));
        }
        if (!valid) {
            GST_ELEMENT_ERROR(transform,
                              STREAM,
                              FORMAT,
                              ("memory:DMABuf caps require DMA-BUF-backed video memory."),
                              (nullptr));
            return GST_FLOW_ERROR;
        }
    }
    const bool dmaBuf = opk::mediaio::gst::GstVideoFrame::hasDmaBufContent(input);
    if (writePixels && dmaBuf && !copyPixels) {
        bool writable = gst_buffer_is_writable(input) && gst_buffer_is_all_memory_writable(input);
        for (guint i = 0; writable && i < gst_buffer_n_memory(input); ++i) {
            writable = !GST_MEMORY_IS_READONLY(gst_buffer_peek_memory(input, i));
        }
        if (!writable) {
            GST_ELEMENT_ERROR(transform,
                              RESOURCE,
                              WRITE,
                              ("DMA-BUF is shared or read-only; drawing would require a copy. "
                               "Set copy-input=true on opkosd to use a CPU copy."),
                              (nullptr));
            return GST_FLOW_ERROR;
        }
    }
    if (copyPixels) {
        *output = gst_buffer_copy_deep(input);
        if (*output != nullptr && !opk::mediaio::gst::GstVideoFrame::hasDirectCpuAddress(*output)) {
            gst_clear_buffer(output);
        }
    } else if (gst_buffer_is_writable(input)) {
        *output = input;
    } else {
        // Only the header and metadata need to be writable; retain the pixel allocation.
        if (dmaBuf) {
            for (guint i = 0; i < gst_buffer_n_memory(input); ++i) {
                if (GST_MEMORY_IS_NO_SHARE(gst_buffer_peek_memory(input, i))) {
                    GST_ELEMENT_ERROR(transform,
                                      RESOURCE,
                                      READ,
                                      ("DMA-BUF cannot be shared. Set copy-input=true on the "
                                       "first opkinfer to use CPU buffers."),
                                      (nullptr));
                    return GST_FLOW_ERROR;
                }
            }
        }
        *output = gst_buffer_copy(input);
        if (dmaBuf && *output != nullptr) {
            for (guint i = 0; i < gst_buffer_n_memory(input); ++i) {
                if (gst_buffer_peek_memory(input, i) != gst_buffer_peek_memory(*output, i)) {
                    gst_clear_buffer(output);
                    GST_ELEMENT_ERROR(transform,
                                      RESOURCE,
                                      READ,
                                      ("Could not preserve DMA-BUF memory. Set copy-input=true "
                                       "on the first opkinfer to use CPU buffers."),
                                      (nullptr));
                    return GST_FLOW_ERROR;
                }
            }
        }
    }
    if (*output == nullptr) {
        GST_ELEMENT_ERROR(transform,
                          RESOURCE,
                          READ,
                          ("Could not prepare the video buffer. The source must provide "
                           "linear CPU-readable video; copying cannot fix an unmappable layout."),
                          (nullptr));
        return GST_FLOW_ERROR;
    }
    return GST_FLOW_OK;
}

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
