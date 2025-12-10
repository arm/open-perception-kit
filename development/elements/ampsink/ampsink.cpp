/* Build:
g++ -fPIC -shared -o libgstampsink.so ampsink.cpp \
  $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-video-1.0 gstreamer-audio-1.0)
*/

#include <gst/audio/audio.h>
#include <gst/gst.h>
#include <gst/gstutils.h>
#include <gst/video/video.h>

#include "amp/Tools.h"
#include "gst/gstelement.h"

#include <cpp-httplib/httplib.h>
#include <memory>
#include <thread>

#ifndef PACKAGE
#define PACKAGE "ampsink"
#endif

/* =============================== AmpSink ============================== */

#define STUN_SERVER "stun://stun.l.google.com:19302"

typedef struct _GstAmpSink GstAmpSink;
typedef struct _GstAmpSinkClass GstAmpSinkClass;

std::thread http_server_thread;
std::unique_ptr<httplib::Server> http_server;

struct _GstAmpSink {
    GstBin parent;

    GstElement *vconv;
    GstElement *queue;
    GstElement *vp8enc;
    GstElement *rtpvp8pay;
    GstElement *webrtcbin;

    /* properties */
    gchar *host;
    gchar *static_files_location;
    gint http_port;
    gint ws_port;
};

struct _GstAmpSinkClass {
    GstBinClass parent_class;
};

GType gst_amp_sink_get_type(void);
#define GST_TYPE_AMP_SINK (gst_amp_sink_get_type())
G_DEFINE_TYPE(GstAmpSink, gst_amp_sink, GST_TYPE_BIN)

/* ===== Utils ===== */

static gboolean have_element(const char *name) {
    GstElementFactory *f = gst_element_factory_find(name);
    if (f) {
        gst_object_unref(f);
        return TRUE;
    }
    return FALSE;
}

static void set_int_if_prop_exists(GstElement *e, const char *prop, gint value) {
    if (!e)
        return;
    GParamSpec *ps = g_object_class_find_property(G_OBJECT_GET_CLASS(e), prop);
    if (ps)
        g_object_set(e, prop, value, NULL);
}

static void push_props_down(GstAmpSink *self) {
    if (self->vp8enc) {
    }
}

/* ===== Properties ===== */
enum {
    PROP_0,
    PROP_HOST,
    PROP_HTTP_PORT,
    PROP_WS_PORT,
    PROP_STATIC_FILES,
};

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
    push_props_down(self);
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
static GstStaticPadTemplate v_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw"));

static GstStaticPadTemplate a_sink_template = GST_STATIC_PAD_TEMPLATE(
    "audiopad", GST_PAD_SINK, GST_PAD_REQUEST, GST_STATIC_CAPS("audio/x-raw"));

/* ===== Request/Release pads (audio, MP2 path + audiorate) ===== */

static GstPad *gst_amp_sink_request_new_pad(GstElement *element,
                                            GstPadTemplate *templ,
                                            const gchar *name,
                                            const GstCaps *caps) {
    auto *self = reinterpret_cast<GstAmpSink *>(element);
    const gchar *templ_name = GST_PAD_TEMPLATE_NAME_TEMPLATE(templ);
    if (g_strcmp0(templ_name, "audiopad") != 0)
        return nullptr;

    /* Apply bitrate now that encoder exists */
    push_props_down(self);

    return nullptr;
}

static void gst_amp_sink_release_pad(GstElement *element, GstPad *pad) {
    gst_element_remove_pad(element, pad);
}

/* ===== Lifecycle ===== */
static void gst_amp_sink_dispose(GObject *object) {
    auto *self = reinterpret_cast<GstAmpSink *>(object);

    if(http_server) {
        http_server->stop();
    }
    http_server_thread.join();

    g_clear_pointer(&self->host, g_free);
    g_clear_pointer(&self->static_files_location, g_free);
    G_OBJECT_CLASS(gst_amp_sink_parent_class)->dispose(object);
}

static void gst_amp_sink_setup_http_server(GstAmpSink *self) {
    using namespace httplib;

    http_server = std::make_unique<Server>();

    auto ret = http_server->set_mount_point("/", self->static_files_location);
    if(!ret) {
        // TODO@ibori: error handling
        throw std::runtime_error("the static file directory doesn't exist");
    }

    http_server->Post("/ctrl", [&](const Request& req, Response& res) {
        // TODO@zoli: handle the contol message
        std::cout << "stop requested\n";
    });

    // TODO@ibori: error handling
    auto started = http_server->listen(self->host, self->http_port);
    if(!ret) {
        // TODO@ibori: error handling
        throw std::runtime_error("http server cannot be started");
    }
}

static void gst_amp_sink_init(GstAmpSink *self) {
    /* defaults */
    self->host = g_strdup(amp::Tools::getLocalIp().c_str());
    self->static_files_location = g_strdup("./scripts/public");
    self->http_port = 9999;
    self->ws_port = 8000;

    self->queue = gst_element_factory_make("queue", "queue");
    self->vp8enc = gst_element_factory_make("vp8enc", "encoder");
    self->rtpvp8pay = gst_element_factory_make("rtpvp8pay", "pay");
    self->webrtcbin = gst_element_factory_make("webrtcbin", "sendrecv");

    /* mandatory children */
    self->vconv = gst_element_factory_make("videoconvert", NULL);

    g_return_if_fail(self->vp8enc && self->rtpvp8pay && self->webrtcbin);

    // STUN server is often needed for real networks
    g_object_set(self->webrtcbin, "stun-server", STUN_SERVER, NULL);
    g_object_set(self->vp8enc, "deadline", 1, NULL);

    /* add & link video → queue → vseg → mux → udpsink */
    gst_bin_add_many(GST_BIN(self),
                     self->vconv,
                     self->queue,
                     self->vp8enc,
                     self->rtpvp8pay,
                     self->webrtcbin,
                     NULL);

    if (!gst_element_link_many(
            self->vconv, self->queue, self->vp8enc, self->rtpvp8pay, NULL)) {
        GST_ERROR_OBJECT(self, "Failed to link video chain");
    }

    GstPad *rtp_src_pad = gst_element_get_static_pad(self->rtpvp8pay, "src");
    GstPad *webrtc_sink_pad = gst_element_request_pad_simple(self->webrtcbin, "sink_%u");
    gst_pad_link(rtp_src_pad, webrtc_sink_pad);
    gst_object_unref(rtp_src_pad);
    gst_object_unref(webrtc_sink_pad);

    /* expose ALWAYS video ghost pad */
    {
        GstPad *vs = gst_element_get_static_pad(self->vconv, "sink");
        GstPad *vg = gst_ghost_pad_new("sink", vs);
        gst_object_unref(vs);
        gst_element_add_pad(GST_ELEMENT(self), vg);
    }

    /* initial property push */
    push_props_down(self);

    http_server_thread = std::thread(gst_amp_sink_setup_http_server, self);
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
        g_param_spec_string(
            "static-files", "Static Files Location", "Location of the static files for HTTP Server", "./scripts/public", kRW));

    /* pads */
    gst_element_class_add_static_pad_template(element_class, &v_sink_template);
    gst_element_class_add_static_pad_template(element_class, &a_sink_template);

    /* request/release handlers for audio */
    element_class->request_new_pad = gst_amp_sink_request_new_pad;
    element_class->release_pad = gst_amp_sink_release_pad;

    gst_element_class_set_static_metadata(element_class,
                                          "AmpSink (video+audio → raw video+audio -> VP8 -> WebRTC)",
                                          "Sink/Network/Bin",
                                          "Encodes & muxes raw video+audio to TS and sends to UDP",
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
