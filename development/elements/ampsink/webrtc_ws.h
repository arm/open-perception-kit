#ifndef __WEBRTC_WS_H__
#define __WEBRTC_WS_H__

#include <gst/gst.h>

#include <memory>

#define ASIO_STANDALONE
#include <asio.hpp>

#include <websocketpp/common/connection_hdl.hpp>
#include <websocketpp/config/asio.hpp>
#include <websocketpp/frame.hpp>
#include <websocketpp/server.hpp>

#define STUN_SERVER "stun://stun.l.google.com:19302"

enum class WebRtcSockerError {
    OK,
};

struct _GstAmpSink;

using ws_server = websocketpp::server<websocketpp::config::asio>;
using connection_hdl = websocketpp::connection_hdl;

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

class WebRtcWebSocket {

    using WebRtcSessions =
        std::map<connection_hdl, std::shared_ptr<SessionContext>, std::owner_less<connection_hdl>>;

    _GstAmpSink *self_;

    std::thread ws_server_thread;

    std::shared_ptr<ws_server> ws = nullptr;

    std::mutex webrtc_session_mutex;
    WebRtcSessions webrtc_sessions;

    void on_open(connection_hdl hdl);
    void on_close(connection_hdl hdl);
    void on_message(connection_hdl hdl, ws_server::message_ptr msg);

    WebRtcSockerError setup();

  public:
    WebRtcWebSocket() = default;
    WebRtcWebSocket(_GstAmpSink *self) : self_(self) {}

    WebRtcSockerError start();
    WebRtcSockerError stop();
};

#endif // !__WEVRTC_WS_H__
