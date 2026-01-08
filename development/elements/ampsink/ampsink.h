#ifndef __AMPSINK_H__
#define __AMPSINK_H__

#include <memory>
#include <mutex>
#include <thread>

#include <amp/Tools.h>

#include <gst/audio/audio.h>
#include <gst/gst.h>
#include <gst/gstelement.h>
#include <gst/gstutils.h>
#include <gst/video/video.h>
#include <gst/webrtc/webrtc.h>

#define ASIO_STANDALONE
#include <asio.hpp>

#include <websocketpp/common/connection_hdl.hpp>
#include <websocketpp/config/asio.hpp>
#include <websocketpp/frame.hpp>
#include <websocketpp/server.hpp>

#include <cpp-httplib/httplib.h>

#include <nlohmann/json.hpp>

#define STUN_SERVER "stun://stun.l.google.com:19302"

struct _GstAmpSinkClass {
    GstBinClass parent_class;
};

typedef struct _GstAmpSink GstAmpSink;
typedef struct _GstAmpSinkClass GstAmpSinkClass;

using ws_server = websocketpp::server<websocketpp::config::asio>;
using connection_hdl = websocketpp::connection_hdl;
using json = nlohmann::json;

struct SessionContext {
    connection_hdl hdl;

    // aliases to make ws_server reachable from session negotiation functions
    std::shared_ptr<ws_server> ws;

    // Per-client GStreamer branch
    GstElement *webrtcbin = nullptr;
    GstElement *queue = nullptr; // between tee and webrtcbin

    GstPad *tee_src_pad = nullptr;     // requested from tee
    GstPad *webrtc_sink_pad = nullptr; // requested from webrtcbin ("sink_%u")
};

struct ToggleStateRequest {
    GstElement *element = nullptr;
    GstState resulting = GST_STATE_NULL;

    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    bool ok = false;
};

struct ToggleInvokeBox {
    std::shared_ptr<ToggleStateRequest> req;
};

using WebRtcSessions =
    std::map<connection_hdl, std::shared_ptr<SessionContext>, std::owner_less<connection_hdl>>;

struct ModelStatus {
    std::string name;
    bool active;
    std::string element_name;
};

struct GstAmpPrivate {
    std::thread http_server_thread;
    std::thread ws_server_thread;
    std::unique_ptr<httplib::Server> http_server;

    std::shared_ptr<ws_server> ws;

    std::mutex webrtc_session_mutex;
    WebRtcSessions webrtc_sessions;

    std::mutex model_registry_mutex;
    std::map<std::string, ModelStatus> model_registry; // key: element_name
};

struct _GstAmpSink {
    GstBin parent;

    GstElement *vconv;
    GstElement *queue;
    GstElement *vp8enc;
    GstElement *rtpvp8pay;
    GstElement *tee;

    // Drain branch to make the pipeline complete
    GstElement *drain_queue;
    GstElement *drain_fakesink;
    GstPad *drain_tee_src_pad;

    /* properties */
    gchar *host;
    gchar *static_files_location;
    gint http_port;
    gint ws_port;

    GstAmpPrivate *private_data;
};

#endif // ! __AMPSINK_H__
