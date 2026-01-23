#ifndef __WEBRTC_WS_H__
#define __WEBRTC_WS_H__

#include <gst/gst.h>
#include <gst/webrtc/webrtc.h>

#include <memory>

#define ASIO_STANDALONE
#include <asio.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtemplate-id-cdtor"

#include <websocketpp/common/connection_hdl.hpp>
#include <websocketpp/config/asio.hpp>
#include <websocketpp/frame.hpp>
#include <websocketpp/server.hpp>

#pragma GCC diagnostic pop

#include <nlohmann/json.hpp>

#define STUN_SERVER "stun://stun.l.google.com:19302"

enum class WebRtcSockerError {
    OK,
};

struct _GstAmpSink;

using ws_server = websocketpp::server<websocketpp::config::asio>;
using connection_hdl = websocketpp::connection_hdl;

// forwards
class WebRtcWebSocket;
struct SessionContext;

class WebRtcWebSocket {

    using WebRtcSessions =
        std::map<connection_hdl, std::shared_ptr<SessionContext>, std::owner_less<connection_hdl>>;

    _GstAmpSink *self_;

    std::thread ws_server_thread;

    std::shared_ptr<ws_server> ws = nullptr;

    std::mutex webrtc_session_mutex;
    WebRtcSessions webrtc_sessions;

    int find_pt_for_codec(const GstSDPMessage *msg,
                          const char *media_type,  // "video" or "audio"
                          const char *codec_name); // "VP8" or "opus"

    gboolean set_audio_pt(SessionContext *ctx);
    bool attach_audio(SessionContext *ctx);

    void set_video_pt(SessionContext *ctx);
    bool attach_video(SessionContext *ctx);

    void link_per_client_elemets(SessionContext *ctx);

    void process_offer(std::shared_ptr<SessionContext> ctx, const nlohmann::json &jsn);
    void process_canditate(std::shared_ptr<SessionContext> ctx, const nlohmann::json &jsn);

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
