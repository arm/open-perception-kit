/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#ifndef __WEBRTC_WS_H__
#define __WEBRTC_WS_H__

#include "glib.h"
#include <gst/gst.h>
#include <gst/webrtc/webrtc.h>

#include <atomic>
#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

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

#include "webrtc_session.h"

enum class WebRtcSockerError {
    OK,
};

struct _GstPekSink;

using ws_server = websocketpp::server<websocketpp::config::asio>;
using connection_hdl = websocketpp::connection_hdl;

// forwards
class WebRtcWebSocket;
struct SessionContext;

class WebRtcWebSocket {

    using WebRtcSessions =
        std::map<connection_hdl, std::shared_ptr<SessionContext>, std::owner_less<connection_hdl>>;

    _GstPekSink *self_;

    std::thread ws_server_thread;

    std::shared_ptr<ws_server> ws = nullptr;
    std::atomic_bool stopping = false;

    mutable std::mutex webrtc_session_mutex;
    WebRtcSessions webrtc_sessions;

    int find_pt_for_codec(const GstSDPMessage *msg,
                          const char *media_type,  // "video" or "audio"
                          const char *codec_name); // "VP8" or "opus"

    gboolean set_audio_pt(SessionContext *ctx);
    bool attach_audio(SessionContext *ctx);

    void set_video_pt(SessionContext *ctx);
    bool attach_video(SessionContext *ctx);

    bool link_per_client_elements(SessionContext *ctx);

    static void on_answer_created(GstPromise *promise, gpointer user_data);
    static void on_set_remote_description(GstPromise *promise, gpointer user_data);

    void process_offer(const std::shared_ptr<SessionContext> &ctx, const nlohmann::json &jsn);
    void process_canditate(const std::shared_ptr<SessionContext> &ctx, const nlohmann::json &jsn);

    void on_open(const connection_hdl &hdl);
    void on_close(const connection_hdl &hdl);
    void on_message(const connection_hdl &hdl, const ws_server::message_ptr &msg);

    std::shared_ptr<SessionContext> get_session(const connection_hdl &hdl);

    WebRtcSockerError setup();

  public:
    WebRtcWebSocket() = default;
    WebRtcWebSocket(_GstPekSink *self) : self_(self) {}

    WebRtcSockerError start();
    WebRtcSockerError stop();

    bool cleanup_session(const connection_hdl &hdl, const char *reason);
    std::size_t active_session_count() const;
};

#endif // !__WEBRTC_WS_H__
