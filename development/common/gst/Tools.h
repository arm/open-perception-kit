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

#pragma once

#include <gst/allocators/gstdmabuf.h>
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>

namespace gst {

struct Tools {

    static constexpr const char *supportedVideoCaps =
        "video/x-raw, format={BGRA,RGB,I420,NV12,YUY2}; "
        "video/x-raw(" GST_CAPS_FEATURE_MEMORY_DMABUF "), format={BGRA,RGB,I420,NV12,YUY2}";

    /**
     * Returns compatible caps for the opposite pad during transform_caps negotiation.
     * Preserves video fields such as format, dimensions and framerate. With
     * copyPixels=false, memory features are also preserved. With copyPixels=true,
     * input may use system memory or DMA-BUF, but output must use system memory.
     * This describes the copy performed by prepareVideoBuffer; it does not copy pixels.
     *
     * @param direction Pad whose capabilities are supplied in caps: GST_PAD_SINK
     *                  asks for possible output caps; GST_PAD_SRC asks for possible
     *                  input caps. In copy mode, a DMA-BUF-only output request has
     *                  no compatible input.
     * @param caps Caps to transform, restricted to the supported raw video formats.
     *             Borrowed; neither modified nor consumed.
     * @param filter Optional restriction on the returned caps. The intersection
     *               preserves the filter's preference order. Borrowed; may be null.
     * @param copyPixels Whether copy-input is enabled; must match buffer preparation.
     * @return Owned caps, possibly empty if no compatible caps remain. The caller
     *         must unref them or transfer ownership to GStreamer.
     */
    static GstCaps *transformVideoCaps(const GstPadDirection direction,
                                       GstCaps *const caps,
                                       GstCaps *const filter,
                                       const bool copyPixels);

    /**
     * Prepares output buffer for a GstBaseTransform::prepare_output_buffer callback.
     *
     * By default, reuses a writable input buffer or copies its header and metadata
     * while sharing pixel memory. For DMA-BUF input, verifies that every GstMemory
     * object is retained; failure to share is an error, not an implicit pixel copy.
     * A writable GstBuffer permits metadata changes but does not imply writable
     * pixel memory. CPU mapping and drawing are left to the caller.
     *
     * @param transform Element on which preparation errors shall be reported.
     * @param input Input buffer
     * @param output Receives input without an extra reference, or a newly owned
     *               buffer, as required by GstBaseTransform. Null on failure.
     * @param copyPixels Explicitly deep-copies the pixels instead of preserving
     *                   the allocation when it is true. Takes precedence
     *                   over writePixels checks; an unmappable layout can still fail.
     * @param writePixels Tell's the function whether the caller will modify pixels.
     *                    Without copyPixels, rejects DMA-BUF input if the buffer or
     *                    any memory is shared or read-only, and suggests copy-input=true.
     *                    Leave false for inference or metadata-only processing.
     * @return GST_FLOW_OK on success, or GST_FLOW_ERROR with an element error.
     */
    static GstFlowReturn prepareVideoBuffer(GstBaseTransform *const transform,
                                            GstBuffer *const input,
                                            GstBuffer **const output,
                                            const bool copyPixels = false,
                                            const bool writePixels = false);

    static GstElement *getOverlayElement(GstVideoFilter *videoFilter);

    static void releaseElement(GstElement *element) {
        gst_object_unref(element);
    }
};
} // namespace gst
