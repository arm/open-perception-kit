/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#ifndef __AMPSINK_H__
#define __AMPSINK_H__

#include <memory>

#include <amp/Tools.h>

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

struct _GstAmpSinkClass {
    GstBinClass parent_class;
};

typedef struct _GstAmpSink GstAmpSink;
typedef struct _GstAmpSinkClass GstAmpSinkClass;

class PipelineStateReporter : public StatusReporter {
    GstAmpSink *self_ = nullptr;

    bool has_audio_ = false;

  public:
    explicit PipelineStateReporter(GstAmpSink *self) : self_(self) {}

    inline void set_paused() {
        trigger_reporting();
    }

    inline void set_audio(bool has_audio) {
        has_audio_ = has_audio;
        trigger_reporting();
    }

    nlohmann::json report() const override;
};

class PerformanceOverlayStateReporter : public StatusReporter {
    GstAmpSink *self_ = nullptr;

  public:
    explicit PerformanceOverlayStateReporter(GstAmpSink *self) : self_(self) {}

    nlohmann::json report() const override;
};

struct GstAmpPrivate {
    std::unique_ptr<AmpSinkHttpServer> http_server;

    std::unique_ptr<WebRtcWebSocket> webrtc_websocket;
    std::unique_ptr<CtrlWebSocket> ctrl_websocket;

    std::shared_ptr<ModelRegistry> model_registry;
    std::shared_ptr<PipelineStateReporter> pipeline_state_reporter;
    std::shared_ptr<PerformanceOverlayStateReporter> performance_overlay_state_reporter;
};

struct _GstAmpSink {
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
    gint http_port;
    gint ctrl_port;
    gint ws_port;

    GstAmpPrivate *private_data;
};

#endif // ! __AMPSINK_H__
