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

#ifndef __UTILS_H__

#include <gst/gstelement.h>

#include <string>

GstElement *get_top_pipeline(GstElement *elem);
GstElement *get_element_by_name(GstElement *top_level, const std::string &element_name);
GstElement *get_element_by_type(GstElement *top_level, const std::string &type_name);

void dump_sink_pads(GstElement *element);

// set this environment variable to get this to work: GST_DEBUG_DUMP_DOT_DIR
void dump_pipeline_graph(GstElement *element, const std::string &file_name);

void release_request_pad_and_unref(GstElement *elem, GstPad **ppad) noexcept;
void remove_pad_if_present(GstElement *elem, GstPad **ppad);

bool set_state_elements_many(GstState state, std::initializer_list<GstElement *> elems) noexcept;

#endif // !__UTILS_H__
