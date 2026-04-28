/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

/* Build:
g++ -fPIC -shared -o libgstampsink.so ampsink.cpp \
  $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-video-1.0 gstreamer-audio-1.0)
*/

// WebRTC in GST is unstable: this macro disables the warning
#include "glib-object.h"
#include "glib.h"
#include "gst/gstobject.h"
#include <gst/gstelement.h>

#define GST_USE_UNSTABLE_API

#include "ampsink.h"
#include "auxiliary.h"
#include "http_server.h"
#include "utils.h"
#include "webrtc_ws.h"

#include <websocketpp/common/connection_hdl.hpp>
#include <websocketpp/config/asio.hpp>
#include <websocketpp/frame.hpp>
#include <websocketpp/server.hpp>

#include <amp/Tools.h>

#include <httplib.h>

#include <nlohmann/json.hpp>

#include <memory>

#ifndef PACKAGE
#define PACKAGE "ampsink"
#endif

#ifndef AMP_DEFAULT_STATIC_FILES_LOCATION
#define AMP_DEFAULT_STATIC_FILES_LOCATION "./development/web/content"
#endif

/* =============================== AmpSink ============================== */

/* ===== Properties ===== */
enum {
    PROP_0,
    PROP_HOST,
    PROP_HTTP_PORT,
    PROP_WS_PORT,
    PROP_CTRL_PORT,
    PROP_STATIC_FILES,
};
nlohmann::json PipelineStateReporter::report() const {
    nlohmann::json ret;

    if (self_) {
        // get the playing state
        GstState cur = GST_STATE_NULL;
        GstState pending = GST_STATE_NULL;
        gst_element_get_state(GST_ELEMENT(self_), &cur, &pending, 0);
        DBG("current state: {}, {}", int(cur), int(pending));

        ret["playing"] = (cur == GST_STATE_PLAYING ? true : false);

        // audio state
        ret["audio"] = has_audio_;
    }

    return ret;
}
nlohmann::json PerformanceOverlayStateReporter::report() const {
    nlohmann::json ret;

    ret["has_performance_overlay"] = false;
    ret["enabled"] = false;

    if (!self_) {
        return ret;
    }

    // we suppose here that only one ampperformance element exists in the pipeline
    auto top = get_top_pipeline(GST_ELEMENT(self_));
    if (!top) {
        return ret;
    }

    auto perf_ovr = get_element_by_type(top, "ampperformance");
    gst_object_unref(top);

    if (perf_ovr && GST_IS_ELEMENT(perf_ovr)) {
        ret["has_performance_overlay"] = true;

        gboolean enabled;
        g_object_get(perf_ovr, "enabled", &enabled, NULL);
        ret["enabled"] = bool(enabled);

        gst_object_unref(perf_ovr);
    }

    return ret;
}

GType gst_amp_sink_get_type(void);
#define GST_TYPE_AMP_SINK (gst_amp_sink_get_type())
G_DEFINE_TYPE(GstAmpSink, gst_amp_sink, GST_TYPE_BIN)

static void
gst_amp_sink_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    auto *self = reinterpret_cast<GstAmpSink *>(object);
    switch (prop_id) {
    case PROP_HOST:
        g_free(self->host);
        self->host = g_value_dup_string(value);
        break;
    case PROP_HTTP_PORT:
        self->http_port = g_value_get_int(value);
        break;
    case PROP_WS_PORT:
        self->ws_port = g_value_get_int(value);
        break;
    case PROP_CTRL_PORT:
        self->ctrl_port = g_value_get_int(value);
        break;
    case PROP_STATIC_FILES:
        g_free(self->static_files_location);
        self->static_files_location = g_value_dup_string(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        return;
    }
}

static void
gst_amp_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    auto *self = reinterpret_cast<GstAmpSink *>(object);
    switch (prop_id) {
    case PROP_HOST:
        g_value_set_string(value, self->host);
        break;
    case PROP_HTTP_PORT:
        g_value_set_int(value, self->http_port);
        break;
    case PROP_WS_PORT:
        g_value_set_int(value, self->ws_port);
        break;
    case PROP_CTRL_PORT:
        g_value_set_int(value, self->ctrl_port);
        break;
    case PROP_STATIC_FILES:
        g_value_set_string(value, self->static_files_location);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

/* ===== Pad templates ===== */
static GstStaticPadTemplate v_sink_template = GST_STATIC_PAD_TEMPLATE(
    "videosink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw"));

static GstStaticPadTemplate a_sink_template = GST_STATIC_PAD_TEMPLATE(
    "audiosink", GST_PAD_SINK, GST_PAD_REQUEST, GST_STATIC_CAPS("audio/x-raw"));

/* ===== Request/Release pads (audio, MP2 path + audiorate) ===== */

static GstPad *gst_amp_sink_request_new_pad(GstElement *element,
                                            GstPadTemplate *templ,
                                            const gchar *name,
                                            const GstCaps *caps) {
    auto *self = reinterpret_cast<GstAmpSink *>(element);
    const gchar *templ_name = GST_PAD_TEMPLATE_NAME_TEMPLATE(templ);

    if (g_strcmp0(templ_name, "audiosink") != 0) {
        return nullptr;
    }

    if (self->private_data && self->private_data->pipeline_state_reporter) {
        self->private_data->pipeline_state_reporter->set_audio(true);
    }

    // If already created, just return existing pad
    // This is probably not the best approach:
    //      one request -> one new pad
    //      what happens if somebody requests new pad,
    //      and we return an already-connected-pad
    if (self->audio_ghost_pad) {
        return nullptr;
    }

    // IMPORTANT: ghost an UNLINKED internal pad (the queue sink), not selector/request pads,
    // and not aconv sink if it's already linked in the internal chain.
    if (!self->ain_queue) {
        GST_ERROR_OBJECT(self, "audio_in_queue is NULL (init_audio not run?)");
        return nullptr;
    }

    GstPad *qin_sink = gst_element_get_static_pad(self->ain_queue, "sink");
    if (!qin_sink) {
        GST_ERROR_OBJECT(self, "Failed to get audio_in_queue:sink");
        return nullptr;
    }

    // Create REQUEST ghost pad
    self->audio_ghost_pad = gst_ghost_pad_new("audiosink", qin_sink);
    gst_object_unref(qin_sink);

    if (!self->audio_ghost_pad) {
        GST_ERROR_OBJECT(self, "Failed to create audio ghost pad");
        return nullptr;
    }

    gst_pad_set_active(self->audio_ghost_pad, TRUE);

    if (!gst_element_add_pad(GST_ELEMENT(self), self->audio_ghost_pad)) {
        GST_ERROR_OBJECT(self, "Failed to add audio ghost pad to element");
        gst_object_unref(self->audio_ghost_pad);
        self->audio_ghost_pad = nullptr;
        return nullptr;
    }

    // switch selector immediately to the "real" pad.
    // this can be done in pad probe. it would be better
    if (self->aselector && self->aselector_real_pad) {
        g_object_set(self->aselector, "active-pad", self->aselector_real_pad, NULL);
        GST_INFO_OBJECT(self, "Audio request pad created, selector switched to real audio input");
    } else {
        GST_WARNING_OBJECT(self, "Selector or real pad not ready; cannot switch to real input");
    }

    return self->audio_ghost_pad;
}

static void gst_amp_sink_release_pad(GstElement *element, GstPad *pad) {
    auto *self = reinterpret_cast<GstAmpSink *>(element);
    gst_element_remove_pad(element, pad);
    if (pad == self->audio_ghost_pad) {
        self->audio_ghost_pad = nullptr;
    }
}

static void gst_amp_sink_stop_pipeline_async(GstElement *element, gpointer user_data) {
    auto *self = reinterpret_cast<GstAmpSink *>(element);
    GstElement *top = get_top_pipeline(element);

    // GST_IS_ELEMENT() is a macro performing a type check with no side effects
    if (top && GST_IS_ELEMENT(top)) { // NOSONAR
        GST_INFO_OBJECT(self, "EOS received, posting EOS message to top pipeline");
        gst_element_post_message(top, gst_message_new_eos(GST_OBJECT(top)));
        gst_object_unref(top);
        return;
    }
}

/* ===== Event handling ===== */
static gboolean gst_amp_sink_sink_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    auto *self = reinterpret_cast<GstAmpSink *>(parent);

    if (GST_EVENT_TYPE(event) == GST_EVENT_CUSTOM_DOWNSTREAM) {
        const GstStructure *structure = gst_event_get_structure(event);

        if (gst_structure_has_name(structure, "amp-model-register")) {
            const gchar *model_name = gst_structure_get_string(structure, "model-name");
            const gchar *element_name = gst_structure_get_string(structure, "element-name");
            gboolean active = FALSE;
            gst_structure_get_boolean(structure, "active", &active);

            if (model_name && element_name) {
                self->private_data->model_registry->add_model(model_name, element_name, active);
            }

            // Consume the event (don't pass it further)
            gst_event_unref(event);
            return TRUE;
        } else if (gst_structure_has_name(structure, "amp-model-unregister")) {
            const gchar *element_name = gst_structure_get_string(structure, "element-name");

            if (element_name) {
                self->private_data->model_registry->del_model(element_name);
            }

            // Consume the event (don't pass it further)

            gst_event_unref(event);
            return TRUE;
        }
    }

    if (GST_EVENT_TYPE(event) == GST_EVENT_EOS) {
        GST_INFO_OBJECT(self, "End of stream received at sink pad event");
        gst_element_call_async(
            GST_ELEMENT(self), gst_amp_sink_stop_pipeline_async, nullptr, nullptr);
        // Forward EOS to the target pad to notify downstream elements of stream end
    }

    // Pass all events (including EOS) to the target pad
    GstPad *target = gst_ghost_pad_get_target(GST_GHOST_PAD(pad));
    if (target) {
        gboolean ret = gst_pad_send_event(target, event);
        gst_object_unref(target);
        return ret;
    }

    gst_event_unref(event);
    return FALSE;
}

/* ===== Lifecycle ===== */
static void gst_amp_sink_dispose(GObject *object) {
    if (!object) {
        return;
    }
    auto *self = reinterpret_cast<GstAmpSink *>(object);

    if (self->private_data) {
        // Stop ctrl_websocket first to ensure callbacks are no longer active before destroying
        // reporters
        if (self->private_data->ctrl_websocket) {
            self->private_data->ctrl_websocket->stop();
            self->private_data->ctrl_websocket.reset();
        }
        // Now reset reporters after ctrl_websocket is destroyed (no more callbacks referencing
        // them)
        self->private_data->pipeline_state_reporter.reset();
        self->private_data->performance_overlay_state_reporter.reset();

        if (self->private_data->http_server) {
            self->private_data->http_server->stop();
            self->private_data->http_server.reset();
        }
        if (self->private_data->webrtc_websocket) {
            self->private_data->webrtc_websocket->stop();
            self->private_data->webrtc_websocket.reset();
        }
    }

    // IMPORTANT: selector may be holding a ref via active-pad
    if (self->aselector) {
        g_object_set(self->aselector, "active-pad", NULL, NULL);
    }

    release_request_pad_and_unref(self->aselector, &self->aselector_silence_pad);
    release_request_pad_and_unref(self->aselector, &self->aselector_real_pad);

    release_request_pad_and_unref(self->tee, &self->drain_tee_src_pad);
    release_request_pad_and_unref(self->atee, &self->audio_drain_tee_src_pad);

    G_OBJECT_CLASS(gst_amp_sink_parent_class)->dispose(object);
}

static void gst_amp_sink_finalize(GObject *object) {
    auto *self = reinterpret_cast<GstAmpSink *>(object);

    // Free properties
    g_clear_pointer(&self->host, g_free);
    g_clear_pointer(&self->static_files_location, g_free);

    // Free private data
    delete self->private_data;
    self->private_data = nullptr;

    G_OBJECT_CLASS(gst_amp_sink_parent_class)->finalize(object);
}

static void init_video(GstAmpSink *self) {

    self->vconv = gst_element_factory_make("videoconvert", "vconv");
    self->queue = gst_element_factory_make("queue", "vqueue");
    self->vp8enc = gst_element_factory_make("vp8enc", "vp8enc");
    self->vclock = gst_element_factory_make("identity", "vclock");
    self->tee = gst_element_factory_make("tee", "rtp_tee");

    g_return_if_fail(self->vconv && self->queue && self->vp8enc && self->tee && self->vclock);

    g_object_set(self->vclock, "sync", TRUE, NULL);
    g_object_set(self->vp8enc, "deadline", 1, NULL);
    g_object_set(self->vp8enc, "keyframe-max-dist", 30, NULL); // keyframe every ~1s at 30fps

    gst_bin_add_many(
        GST_BIN(self), self->vconv, self->queue, self->vp8enc, self->vclock, self->tee, NULL);

    if (!gst_element_link_many(
            self->vconv, self->queue, self->vp8enc, self->vclock, self->tee, NULL)) {
        GST_ERROR_OBJECT(self, "Failed to link video chain");
    }

    /* --- DRAIN BRANCH: tee → drain_queue → fakesink --- */
    self->drain_queue = gst_element_factory_make("queue", "drain_queue");
    self->drain_fakesink = gst_element_factory_make("fakesink", "drain_fakesink");

    g_return_if_fail(self->drain_queue && self->drain_fakesink);

    // fakesink should not block or sync to clock
    g_object_set(self->drain_fakesink, "sync", FALSE, "async", FALSE, NULL);

    gst_bin_add_many(GST_BIN(self), self->drain_queue, self->drain_fakesink, NULL);

    if (!gst_element_link(self->drain_queue, self->drain_fakesink)) {
        GST_ERROR_OBJECT(self, "Failed to link drain_queue -> drain_fakesink");
    }

    // Connect tee → drain_queue
    self->drain_tee_src_pad = gst_element_request_pad_simple(self->tee, "src_%u");
    if (!self->drain_tee_src_pad) {
        GST_ERROR_OBJECT(self, "Failed to request src pad from tee for drain");
    } else {
        GstPad *drain_sink = gst_element_get_static_pad(self->drain_queue, "sink");
        if (!drain_sink) {
            GST_ERROR_OBJECT(self, "Failed to get sink pad of drain_queue");
        } else {
            if (gst_pad_link(self->drain_tee_src_pad, drain_sink) != GST_PAD_LINK_OK) {
                GST_ERROR_OBJECT(self, "Failed to link tee -> drain_queue");
            }
            gst_object_unref(drain_sink);
        }
    }

    // Make sure drain elements follow the bin state
    gst_element_sync_state_with_parent(self->drain_queue);
    gst_element_sync_state_with_parent(self->drain_fakesink);

    /* expose ALWAYS video ghost pad */
    {
        GstPad *vs = gst_element_get_static_pad(self->vconv, "sink");
        GstPad *vg = gst_ghost_pad_new("sink", vs);
        gst_object_unref(vs);

        // Install custom event handler
        gst_pad_set_event_function(vg, gst_amp_sink_sink_event);
        gst_element_add_pad(GST_ELEMENT(self), vg);
    }
}

static void init_audio(GstAmpSink *self) {
    // //////////////////////////////////////////////
    // AUDIO is always created and add silence to it
    // /////////////////////////////////////////////

    // Create audio elements
    self->asilence_src = gst_element_factory_make("audiotestsrc", "audio_silence_src");
    self->ain_queue = gst_element_factory_make("queue", "audio_in_queue");
    self->aselector = gst_element_factory_make("input-selector", "audio_selector");
    self->acapsfilter = gst_element_factory_make("capsfilter", "audio_caps");
    self->aconv = gst_element_factory_make("audioconvert", "aconv");
    self->aresample = gst_element_factory_make("audioresample", "aresample");
    self->opusenc = gst_element_factory_make("opusenc", "opusenc");
    self->aclock = gst_element_factory_make("identity", "aclock");
    self->atee = gst_element_factory_make("tee", "audio_tee");

    if (!self->asilence_src || !self->aselector || !self->aconv || !self->aresample ||
        !self->opusenc || !self->aclock || !self->atee || !self->ain_queue || !self->acapsfilter) {
        GST_ERROR_OBJECT(self, "Failed to create audio elements");
    }

    g_object_set(self->asilence_src, "wave", 4 /* silence */, "is-live", TRUE, NULL);

    GstCaps *audio_caps = gst_caps_new_simple("audio/x-raw",
                                              "format",
                                              G_TYPE_STRING,
                                              "S16LE",
                                              "rate",
                                              G_TYPE_INT,
                                              48000,
                                              "channels",
                                              G_TYPE_INT,
                                              2,
                                              NULL);
    g_object_set(self->acapsfilter, "caps", audio_caps, NULL);
    g_object_set(self->aclock, "sync", TRUE, NULL);
    gst_caps_unref(audio_caps);

    // Add + link shared audio chain
    gst_bin_add_many(GST_BIN(self),
                     self->asilence_src,
                     self->ain_queue,
                     self->aselector,
                     self->aconv,
                     self->aresample,
                     self->acapsfilter,
                     self->opusenc,
                     self->aclock,
                     self->atee,
                     NULL);

    // link the silence path to the selector
    GstPad *silence_src_pad = gst_element_get_static_pad(self->asilence_src, "src");
    self->aselector_silence_pad = gst_element_request_pad_simple(self->aselector, "sink_%u");
    auto lret = gst_pad_link(silence_src_pad, self->aselector_silence_pad);
    gst_object_unref(silence_src_pad);

    if (lret != GST_PAD_LINK_OK) {
        GST_ERROR_OBJECT(self, "Failed to link silence into selector: %d", lret);
    }

    // Link REAL input queue into selector (pad link)
    GstPad *real_src = gst_element_get_static_pad(self->ain_queue, "src");
    self->aselector_real_pad = gst_element_request_pad_simple(self->aselector, "sink_%u");
    lret = gst_pad_link(real_src, self->aselector_real_pad);
    gst_object_unref(real_src);
    if (lret != GST_PAD_LINK_OK) {
        GST_ERROR_OBJECT(self, "Failed to link real audio into selector: %d", lret);
        return;
    }

    // selector -> opusenc -> tee
    if (!gst_element_link_many(self->aselector,
                               self->aconv,
                               self->aresample,
                               self->acapsfilter,
                               self->opusenc,
                               self->aclock,
                               self->atee,
                               NULL)) {
        GST_ERROR_OBJECT(self, "Failed to link selector->opusenc->tee chain");
    }

    // Drain branch so pipeline can PLAY with no clients
    self->audio_drain_queue = gst_element_factory_make("queue", "audio_drain_queue");
    self->audio_drain_fakesink = gst_element_factory_make("fakesink", "audio_drain_fakesink");

    g_object_set(self->audio_drain_fakesink, "sync", FALSE, "async", FALSE, NULL);

    gst_bin_add_many(GST_BIN(self), self->audio_drain_queue, self->audio_drain_fakesink, NULL);

    gst_element_link(self->audio_drain_queue, self->audio_drain_fakesink);

    // tee → drain
    self->audio_drain_tee_src_pad = gst_element_request_pad_simple(self->atee, "src_%u");
    GstPad *drain_sink_pad = gst_element_get_static_pad(self->audio_drain_queue, "sink");
    lret = gst_pad_link(self->audio_drain_tee_src_pad, drain_sink_pad);
    gst_object_unref(drain_sink_pad);
    if (lret != GST_PAD_LINK_OK) {
        GST_ERROR_OBJECT(self, "Failed to link audio drain branch: %d", lret);
    }

    // Sync state
    gst_element_sync_state_with_parent(self->asilence_src);
    gst_element_sync_state_with_parent(self->ain_queue);
    gst_element_sync_state_with_parent(self->aselector);
    gst_element_sync_state_with_parent(self->aconv);
    gst_element_sync_state_with_parent(self->aresample);
    gst_element_sync_state_with_parent(self->acapsfilter);
    gst_element_sync_state_with_parent(self->opusenc);
    gst_element_sync_state_with_parent(self->atee);
    gst_element_sync_state_with_parent(self->audio_drain_queue);
    gst_element_sync_state_with_parent(self->audio_drain_fakesink);
}

static void gst_amp_sink_init(GstAmpSink *self) {
    self->private_data = new GstAmpPrivate();

    /* defaults */
    self->host = g_strdup(amp::Tools::getLocalIp().c_str());
    self->static_files_location = g_strdup(AMP_DEFAULT_STATIC_FILES_LOCATION);
    self->http_port = 9999;
    self->ws_port = 8000;
    self->ctrl_port = 8001;

    init_video(self);
    init_audio(self);

    // private data
    self->private_data->model_registry = std::make_shared<ModelRegistry>();

    self->private_data->webrtc_websocket = std::make_unique<WebRtcWebSocket>(self);
    self->private_data->webrtc_websocket->start();

    self->private_data->ctrl_websocket = std::make_unique<CtrlWebSocket>(self);
    self->private_data->ctrl_websocket->start();

    self->private_data->http_server = std::make_unique<AmpSinkHttpServer>(self);
    self->private_data->http_server->start();

    self->private_data->pipeline_state_reporter = std::make_shared<PipelineStateReporter>(self);
    self->private_data->performance_overlay_state_reporter =
        std::make_shared<PerformanceOverlayStateReporter>(self);

    self->private_data->ctrl_websocket->register_status_reporter(
        "models", self->private_data->model_registry);
    self->private_data->ctrl_websocket->register_status_reporter(
        "pipeline_state", self->private_data->pipeline_state_reporter);
    self->private_data->ctrl_websocket->register_status_reporter(
        "perf_overlay", self->private_data->performance_overlay_state_reporter);
}

static void gst_amp_sink_class_init(GstAmpSinkClass *klass) {
    auto *gobject_class = G_OBJECT_CLASS(klass);
    auto *element_class = GST_ELEMENT_CLASS(klass);

    gobject_class->set_property = gst_amp_sink_set_property;
    gobject_class->get_property = gst_amp_sink_get_property;
    gobject_class->dispose = gst_amp_sink_dispose;
    gobject_class->finalize = gst_amp_sink_finalize;

    /* C++ flags helper */
    constexpr GParamFlags kRW =
        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /* properties */
    g_object_class_install_property(
        gobject_class,
        PROP_HOST,
        g_param_spec_string(
            "host", "Host", "The interface to bind the HTTP Server", "0.0.0.0", kRW));
    g_object_class_install_property(
        gobject_class,
        PROP_HTTP_PORT,
        g_param_spec_int("http-port", "HTTP Port", "HTTP Server Port", 1, 65535, 9999, kRW));
    g_object_class_install_property(
        gobject_class,
        PROP_WS_PORT,
        g_param_spec_int("ws-port", "WebSocket Port", "WebSocket Port", 1, 65535, 8000, kRW));
    g_object_class_install_property(
        gobject_class,
        PROP_CTRL_PORT,
        g_param_spec_int("ctrl-port", "Control Port", "Control Port", 1, 65535, 8001, kRW));
    g_object_class_install_property(
        gobject_class,
        PROP_STATIC_FILES,
        g_param_spec_string("static-files",
                            "Static Files Location",
                            "Location of the static files for HTTP Server",
                            AMP_DEFAULT_STATIC_FILES_LOCATION,
                            kRW));

    /* pads */
    gst_element_class_add_static_pad_template(element_class, &v_sink_template);
    gst_element_class_add_static_pad_template(element_class, &a_sink_template);

    /* request/release handlers for audio */
    element_class->request_new_pad = gst_amp_sink_request_new_pad;
    element_class->release_pad = gst_amp_sink_release_pad;

    gst_element_class_set_static_metadata(
        element_class,
        "AmpSink (video+audio → raw video+audio -> VP8 -> WebRTC)",
        "Sink/Network/Bin",
        "Encodes & muxes raw video+audio and sends them to WebRTC",
        "Your Name <you@example.com>");
}

/* ===== Plugin boilerplate ===== */
static gboolean plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "ampsink", GST_RANK_NONE, GST_TYPE_AMP_SINK);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  ampsink,
                  "AmpSink bin: raw video+audio -> VP8 -> WebRTC ",
                  plugin_init,
                  "1.0",
                  "LGPL",
                  PACKAGE,
                  "https://example.com")
