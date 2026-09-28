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

#ifndef __OPKSINK_H__
#define __OPKSINK_H__

#include <memory>

#include <opk/Tools.h>

#include <gst/audio/audio.h>
#include <gst/gst.h>
#include <gst/gstelement.h>
#include <gst/gstutils.h>
#include <gst/video/video.h>
#include <gst/webrtc/webrtc.h>

#include "ctrl_ws.h"
#include "http_server.h"
#include "model_reg.h"
#include "status_reporter.h"
#include "webrtc_ws.h"

struct _GstOpkSinkClass {
    GstBinClass parent_class;
};

typedef struct _GstOpkSink GstOpkSink;
typedef struct _GstOpkSinkClass GstOpkSinkClass;

class PipelineStateReporter : public StatusReporter {
    GstOpkSink *self_ = nullptr;

    bool has_audio_ = false;

  public:
    explicit PipelineStateReporter(GstOpkSink *self) : self_(self) {}

    inline void set_paused() {
        trigger_reporting();
    }

    inline void set_audio(bool has_audio) {
        has_audio_ = has_audio;
        trigger_reporting();
    }

    nlohmann::json report() const override;
};

struct GstOpkPrivate {
    std::unique_ptr<OpkSinkHttpServer> http_server;

    std::unique_ptr<WebRtcWebSocket> webrtc_websocket;
    std::unique_ptr<CtrlWebSocket> ctrl_websocket;

    std::shared_ptr<ModelRegistry> model_registry;
    std::shared_ptr<PipelineStateReporter> pipeline_state_reporter;
    bool initialized = false;
};

struct _GstOpkSink {
    GstBin parent;

    GstElement *vconv = nullptr;
    GstElement *queue = nullptr;
    GstElement *vp8enc = nullptr;
    GstElement *vclock = nullptr;
    GstElement *tee = nullptr;

    // audio
    GstElement *asilence_src = nullptr;
    GstElement *ain_queue = nullptr;
    GstElement *aselector = nullptr;
    GstElement *acapsfilter = nullptr;
    GstElement *aconv = nullptr;
    GstElement *aresample = nullptr;
    GstElement *opusenc = nullptr;
    GstElement *aclock = nullptr;
    GstElement *atee = nullptr;

    GstPad *aselector_silence_pad = nullptr;
    GstPad *aselector_real_pad = nullptr;

    // Drain branch to make the pipeline complete
    GstElement *drain_queue = nullptr;
    GstElement *drain_fakesink = nullptr;
    GstPad *drain_tee_src_pad = nullptr;

    GstElement *audio_drain_queue = nullptr;
    GstElement *audio_drain_fakesink = nullptr;
    GstPad *audio_drain_tee_src_pad = nullptr;

    // The request audio ghost pad (optional to store)
    GstPad *audio_ghost_pad = nullptr;

    /* properties */
    gchar *host;
    gchar *static_files_location;
    gchar *webrtc_stun_server;
    gchar *webrtc_turn_server;
    gint http_port;
    gint ctrl_port;
    gint ws_port;
    gboolean qos_enabled;

    GstOpkPrivate *private_data;
};

#endif // ! __OPKSINK_H__
