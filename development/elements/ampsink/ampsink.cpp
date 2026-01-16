/* Build:
g++ -fPIC -shared -o libgstampsink.so ampsink.cpp \
  $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-video-1.0 gstreamer-audio-1.0)
*/

#include "ampsink.h"
#include "http_server.h"
#include "webrtc_ws.h"

#include <memory>

#ifndef PACKAGE
#define PACKAGE "ampsink"
#endif

/* =============================== AmpSink ============================== */

/* ===== Properties ===== */
enum {
    PROP_0,
    PROP_HOST,
    PROP_HTTP_PORT,
    PROP_WS_PORT,
    PROP_STATIC_FILES,
};

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

    // If already created, just return existing pad
    if (self->audio_ghost_pad) {
        return self->audio_ghost_pad;
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
    gst_element_remove_pad(element, pad);
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

    // Pass all other events (CAPS, SEGMENT, EOS, etc.) to the target pad
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
    auto *self = reinterpret_cast<GstAmpSink *>(object);

    self->private_data->http_server->stop();
    self->private_data->webrtc_websocket->stop();

    g_clear_pointer(&self->host, g_free);
    g_clear_pointer(&self->static_files_location, g_free);

    delete self->private_data;

    G_OBJECT_CLASS(gst_amp_sink_parent_class)->dispose(object);
}

static void init_video(GstAmpSink *self) {

    self->vconv = gst_element_factory_make("videoconvert", "vconv");
    self->queue = gst_element_factory_make("queue", "vqueue");
    self->vp8enc = gst_element_factory_make("vp8enc", "vp8enc");
    self->rtpvp8pay = gst_element_factory_make("rtpvp8pay", "rtpvp8pay");
    self->tee = gst_element_factory_make("tee", "rtp_tee");

    g_return_if_fail(self->vconv && self->queue && self->vp8enc && self->rtpvp8pay && self->tee);

    g_object_set(self->vp8enc, "deadline", 1, NULL);
    g_object_set(self->vp8enc, "keyframe-max-dist", 30, NULL); // keyframe every ~1s at 30fps

    gst_bin_add_many(
        GST_BIN(self), self->vconv, self->queue, self->vp8enc, self->rtpvp8pay, self->tee, NULL);

    if (!gst_element_link_many(
            self->vconv, self->queue, self->vp8enc, self->rtpvp8pay, self->tee, NULL)) {
        GST_ERROR_OBJECT(self, "Failed to link video chain");
    }

    /* --- DRAIN BRANCH: tee → drain_queue → fakesink --- */
    self->drain_queue = gst_element_factory_make("queue", "drain_queue");
    self->drain_fakesink = gst_element_factory_make("fakesink", "drain_fakesink");

    g_return_if_fail(self->drain_queue && self->drain_fakesink);

    // fakesink should not block or syncsto clock
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
    self->rtpopuspay = gst_element_factory_make("rtpopuspay", "rtpopuspay");
    self->atee = gst_element_factory_make("tee", "audio_tee");

    if (!self->asilence_src || !self->aselector || !self->aconv || !self->aresample ||
        !self->opusenc || !self->rtpopuspay || !self->atee) {
        GST_ERROR_OBJECT(self, "Failed to create audio elements");
    }

    g_object_set(self->asilence_src, "wave", 4 /* silence */, "is-live", TRUE, NULL);

    g_object_set(self->rtpopuspay, "pt", 111, NULL);

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
                     self->rtpopuspay,
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

    // selector -> opusenc -> pay -> tee
    if (!gst_element_link_many(self->aselector,
                               self->aconv,
                               self->aresample,
                               self->acapsfilter,
                               self->opusenc,
                               self->rtpopuspay,
                               self->atee,
                               NULL)) {
        GST_ERROR_OBJECT(self, "Failed to link selector->opusenc->pay->tee chain");
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
    gst_element_sync_state_with_parent(self->rtpopuspay);
    gst_element_sync_state_with_parent(self->atee);
    gst_element_sync_state_with_parent(self->audio_drain_queue);
    gst_element_sync_state_with_parent(self->audio_drain_fakesink);
}

static void gst_amp_sink_init(GstAmpSink *self) {
    self->private_data = new GstAmpPrivate();

    /* defaults */
    self->host = g_strdup(amp::Tools::getLocalIp().c_str());
    self->static_files_location = g_strdup("./scripts/public");
    self->http_port = 9999;
    self->ws_port = 8000;

    init_video(self);
    init_audio(self);

    // private data
    self->private_data->model_registry = std::make_unique<ModelRegistry>();

    self->private_data->webrtc_websocket = std::make_unique<WebRtcWebSocket>(self);
    self->private_data->webrtc_websocket->start();

    self->private_data->http_server = std::make_unique<AmpSinkHttpServer>(self);
    self->private_data->http_server->start();
}

static void gst_amp_sink_class_init(GstAmpSinkClass *klass) {
    auto *gobject_class = G_OBJECT_CLASS(klass);
    auto *element_class = GST_ELEMENT_CLASS(klass);

    gobject_class->set_property = gst_amp_sink_set_property;
    gobject_class->get_property = gst_amp_sink_get_property;
    gobject_class->dispose = gst_amp_sink_dispose;

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
        PROP_STATIC_FILES,
        g_param_spec_string("static-files",
                            "Static Files Location",
                            "Location of the static files for HTTP Server",
                            "./scripts/public",
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
