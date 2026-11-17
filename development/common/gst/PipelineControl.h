/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

#include <gst/gst.h>

namespace gst::pipeline_control {

inline constexpr const char *kPipelineControlAvailableQuery = "opk-pipeline-control-available";
inline constexpr const char *kErrorEvent = "opk-pipeline-control-error";
inline constexpr const char *kErrorMessage = "message";

// Preserve connections from pads that their elements recreate after stopping.
GstStateChangeReturn setState(GstElement *const pipeline, const GstState state);

// The application owns the bus and decides whether an error ends the run.
inline bool stopNeededForUserCorrection(GstElement *const pipeline, const char *const message) {
    g_autoptr(GstQuery) pipelineControllerAvailableQuery = gst_query_new_custom(
        GST_QUERY_CUSTOM, gst_structure_new_empty(kPipelineControlAvailableQuery));
    if (!gst_element_query(pipeline, pipelineControllerAvailableQuery)) {
        return false;
    }
    if (setState(pipeline, GST_STATE_READY) == GST_STATE_CHANGE_FAILURE) {
        return false;
    }
    gst_element_send_event(
        pipeline,
        gst_event_new_custom(
            GST_EVENT_CUSTOM_UPSTREAM,
            gst_structure_new(kErrorEvent, kErrorMessage, G_TYPE_STRING, message, nullptr)));
    return true;
}

} // namespace gst::pipeline_control
