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

#ifndef OPKSRC_H
#define OPKSRC_H

#include <gst/gst.h>

class OpkSrcSignaling;

struct _GstOpkSrcClass {
    GstBinClass parent_class;
};

typedef struct _GstOpkSrc GstOpkSrc;
typedef struct _GstOpkSrcClass GstOpkSrcClass;

struct _GstOpkSrc {
    GstBin parent;

    gint ws_port;
    gint rate;
    GstElement *output_filter;
    GstElement *webrtcbin;
    GstElement *depay;
    GstElement *selector;
    GstPad *silence_pad;
    GstPad *audio_pad;
    GstElement *video_selector;
    GstPad *black_pad;
    gulong pad_added_handler;
    gboolean initialized;
    OpkSrcSignaling *signaling;
    GRecMutex media_lock;
    GstState media_state;
};

bool gst_opk_src_reset_webrtc(GstOpkSrc *self);
bool gst_opk_src_init_video(GstOpkSrc *const self);
void gst_opk_src_video_class_init(GstElementClass *const element_class);
void gst_opk_src_dispose_video(GstOpkSrc *const self);

#endif
