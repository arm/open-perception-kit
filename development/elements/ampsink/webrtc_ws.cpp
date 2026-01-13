#include "webrtc_ws.h"
#include "ampsink.h"

#include <nlohmann/json.hpp>

#include <thread>

using namespace nlohmann;

WebRtcSockerError WebRtcWebSocket::setup() {
    std::cout << "started ws server\n";

    ws = std::make_shared<ws_server>();

    ws->init_asio();

    ws->set_open_handler([this](connection_hdl hdl) { on_open(hdl); });
    ws->set_close_handler([this](connection_hdl hdl) { on_close(hdl); });
    ws->set_message_handler(
        [this](connection_hdl hdl, ws_server::message_ptr msg) { on_message(hdl, msg); });

    ws->set_reuse_addr(true);
    ws->listen(self_->ws_port);
    ws->start_accept();

    return WebRtcSockerError::OK;
}

WebRtcSockerError WebRtcWebSocket::start() {
    using namespace std::chrono_literals;

    if (auto error = setup(); error != WebRtcSockerError::OK) {
        return error;
    }
    std::cout << "WebSocket++ server listening on port " << self_->ws_port << std::endl;

    ws_server_thread = std::thread(&ws_server::run, ws);
    while (!ws->is_listening()) {
        std::this_thread::sleep_for(1ms);
    }

    return WebRtcSockerError::OK;
}

WebRtcSockerError WebRtcWebSocket::stop() {

    if (ws) {
        ws->stop();
    }

    if (ws_server_thread.joinable()) {
        ws_server_thread.join();
    }

    return WebRtcSockerError::OK;
}

static void on_negotiation_needed(GstElement *webrtc, gpointer user_data) {
    std::cout << "Negotiation needed" << std::endl;
}

static void send_text(SessionContext *ctx, const std::string &text) {
    try {
        ctx->ws->send(ctx->hdl, text, websocketpp::frame::opcode::text);
    } catch (const websocketpp::exception &e) {
        std::cerr << "WebSocket send error: " << e.what() << std::endl;
    }
}

static void send_ice_candidate_message(SessionContext *ctx, guint mlineindex, gchar *candidate) {
    std::cout << "Sending ICE candidate: mlineindex=" << mlineindex << ", candidate=" << candidate
              << std::endl;
    json msg;
    msg["type"] = "candidate";
    msg["ice"] = {{"candidate", candidate}, {"sdpMLineIndex", mlineindex}};

    send_text(ctx, msg.dump());

    std::cout << "ICE candidate sent" << std::endl;
}

static void
on_ice_candidate(GstElement *webrtc, guint mlineindex, gchar *candidate, gpointer user_data) {
    std::cout << "ICE candidate generated: mlineindex=" << mlineindex << ", candidate=" << candidate
              << std::endl;

    SessionContext *ctx = static_cast<SessionContext *>(user_data);
    send_ice_candidate_message(ctx, mlineindex, candidate);
}

void WebRtcWebSocket::on_open(connection_hdl hdl) {
    std::cout << "WebSocket connection opened" << std::endl;

    auto ctx = std::make_shared<SessionContext>();
    ctx->ws = ws;
    ctx->hdl = hdl;

    // --- Create per-client elements ---
    ctx->queue = gst_element_factory_make("queue", nullptr);
    ctx->webrtcbin = gst_element_factory_make("webrtcbin", nullptr);

    if (!ctx->queue || !ctx->webrtcbin) {
        g_printerr("Failed to create per-client queue or webrtcbin\n");
        if (ctx->queue)
            gst_object_unref(ctx->queue);
        if (ctx->webrtcbin)
            gst_object_unref(ctx->webrtcbin);
        return;
    }

    g_object_set(ctx->webrtcbin, "stun-server", STUN_SERVER, nullptr);

    // Add to ampsink bin
    gst_bin_add_many(GST_BIN(self_), ctx->queue, ctx->webrtcbin, nullptr);

    // === 1) tee → client queue ===
    ctx->tee_src_pad = gst_element_request_pad_simple(self_->tee, "src_%u");
    if (!ctx->tee_src_pad) {
        g_printerr("Failed to request src pad from tee for client branch\n");
        gst_bin_remove_many(GST_BIN(self_), ctx->queue, ctx->webrtcbin, nullptr);
        return;
    }

    GstPad *queue_sink_pad = gst_element_get_static_pad(ctx->queue, "sink");
    if (!queue_sink_pad) {
        g_printerr("Failed to get sink pad of client queue\n");
        gst_element_release_request_pad(self_->tee, ctx->tee_src_pad);
        gst_object_unref(ctx->tee_src_pad);
        ctx->tee_src_pad = nullptr;
        gst_bin_remove_many(GST_BIN(self_), ctx->queue, ctx->webrtcbin, nullptr);
        return;
    }

    if (gst_pad_link(ctx->tee_src_pad, queue_sink_pad) != GST_PAD_LINK_OK) {
        g_printerr("Failed to link tee -> client queue\n");
        gst_object_unref(queue_sink_pad);
        gst_element_release_request_pad(self_->tee, ctx->tee_src_pad);
        gst_object_unref(ctx->tee_src_pad);
        ctx->tee_src_pad = nullptr;
        gst_bin_remove_many(GST_BIN(self_), ctx->queue, ctx->webrtcbin, nullptr);
        return;
    }
    gst_object_unref(queue_sink_pad);

    // === 2) client queue → webrtcbin sink_%u ===
    GstPad *queue_src_pad = gst_element_get_static_pad(ctx->queue, "src");
    if (!queue_src_pad) {
        g_printerr("Failed to get src pad of client queue\n");

        // undo tee → queue
        GstPad *qs = gst_element_get_static_pad(ctx->queue, "sink");
        if (qs) {
            gst_pad_unlink(ctx->tee_src_pad, qs);
            gst_object_unref(qs);
        }
        gst_element_release_request_pad(self_->tee, ctx->tee_src_pad);
        gst_object_unref(ctx->tee_src_pad);
        ctx->tee_src_pad = nullptr;

        gst_bin_remove_many(GST_BIN(self_), ctx->queue, ctx->webrtcbin, nullptr);
        return;
    }

    ctx->webrtc_sink_pad = gst_element_request_pad_simple(ctx->webrtcbin, "sink_%u");
    if (!ctx->webrtc_sink_pad) {
        g_printerr("Failed to request sink pad on webrtcbin\n");
        gst_object_unref(queue_src_pad);

        // undo tee → queue
        GstPad *qs = gst_element_get_static_pad(ctx->queue, "sink");
        if (qs) {
            gst_pad_unlink(ctx->tee_src_pad, qs);
            gst_object_unref(qs);
        }
        gst_element_release_request_pad(self_->tee, ctx->tee_src_pad);
        gst_object_unref(ctx->tee_src_pad);
        ctx->tee_src_pad = nullptr;

        gst_bin_remove_many(GST_BIN(self_), ctx->queue, ctx->webrtcbin, nullptr);
        return;
    }

    if (gst_pad_link(queue_src_pad, ctx->webrtc_sink_pad) != GST_PAD_LINK_OK) {
        g_printerr("Failed to link client queue src -> webrtcbin sink\n");
        gst_object_unref(queue_src_pad);

        gst_element_release_request_pad(ctx->webrtcbin, ctx->webrtc_sink_pad);
        gst_object_unref(ctx->webrtc_sink_pad);
        ctx->webrtc_sink_pad = nullptr;

        // undo tee → queue
        GstPad *qs = gst_element_get_static_pad(ctx->queue, "sink");
        if (qs) {
            gst_pad_unlink(ctx->tee_src_pad, qs);
            gst_object_unref(qs);
        }
        gst_element_release_request_pad(self_->tee, ctx->tee_src_pad);
        gst_object_unref(ctx->tee_src_pad);
        ctx->tee_src_pad = nullptr;

        gst_bin_remove_many(GST_BIN(self_), ctx->queue, ctx->webrtcbin, nullptr);
        return;
    }

    gst_object_unref(queue_src_pad);

    // Make sure new elements follow the bin's current state
    gst_element_sync_state_with_parent(ctx->queue);
    gst_element_sync_state_with_parent(ctx->webrtcbin);

    // Hook WebRTC callbacks to this session
    g_signal_connect(
        ctx->webrtcbin, "on-negotiation-needed", G_CALLBACK(on_negotiation_needed), ctx.get());
    g_signal_connect(ctx->webrtcbin, "on-ice-candidate", G_CALLBACK(on_ice_candidate), ctx.get());

    std::lock_guard<std::mutex> mutex_guard(webrtc_session_mutex);
    webrtc_sessions[hdl] = ctx;

    std::cout << "Per-client WebRTC branch created and attached to tee\n";
}

void WebRtcWebSocket::on_close(connection_hdl hdl) {
    std::cout << "WebSocket connection closed" << std::endl;

    std::lock_guard<std::mutex> mutex_guard(webrtc_session_mutex);
    auto &sessions = webrtc_sessions;

    auto it = sessions.find(hdl);
    if (it == sessions.end()) {
        return;
    }

    auto ctx = it->second;

    // 1) Stop per-client elements
    if (ctx->webrtcbin)
        gst_element_set_state(ctx->webrtcbin, GST_STATE_NULL);
    if (ctx->queue)
        gst_element_set_state(ctx->queue, GST_STATE_NULL);

    // 2) Unlink tee -> client queue and release the tee src pad
    if (ctx->tee_src_pad) {
        GstPad *queue_sink = nullptr;
        if (ctx->queue) {
            queue_sink = gst_element_get_static_pad(ctx->queue, "sink");
        }

        if (queue_sink) {
            gst_pad_unlink(ctx->tee_src_pad, queue_sink);
            gst_object_unref(queue_sink);
        }

        gst_element_release_request_pad(self_->tee, ctx->tee_src_pad);
        gst_object_unref(ctx->tee_src_pad);
        ctx->tee_src_pad = nullptr;
    }

    // 3) Release webrtcbin sink_%u pad
    if (ctx->webrtc_sink_pad && ctx->webrtcbin) {
        gst_element_release_request_pad(ctx->webrtcbin, ctx->webrtc_sink_pad);
        gst_object_unref(ctx->webrtc_sink_pad);
        ctx->webrtc_sink_pad = nullptr;
    }

    // 4) Remove per-client elements from the bin
    if (ctx->queue || ctx->webrtcbin) {
        gst_bin_remove_many(GST_BIN(self_), ctx->queue, ctx->webrtcbin, nullptr);
    }

    ctx->queue = nullptr;
    ctx->webrtcbin = nullptr;

    // 5) Forget the session
    sessions.erase(it);
}

static void on_answer_created(GstPromise *promise, gpointer user_data) {
    std::cout << "Answer created" << std::endl;

    SessionContext *ctx = static_cast<SessionContext *>(user_data);

    GstWebRTCSessionDescription *answer = NULL;
    const GstStructure *reply = gst_promise_get_reply(promise);
    gst_structure_get(reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, NULL);

    GstPromise *local_promise = gst_promise_new();
    g_signal_emit_by_name(ctx->webrtcbin, "set-local-description", answer, local_promise);

    json sdp_json;
    sdp_json["type"] = "answer";
    sdp_json["sdp"] = gst_sdp_message_as_text(answer->sdp);
    send_text(ctx, sdp_json.dump());

    std::cout << "Local description set and answer sent: " << sdp_json.dump() << std::endl;

    gst_webrtc_session_description_free(answer);
}

static void on_set_remote_description(GstPromise *promise, gpointer user_data) {
    std::cout << "Remote description set, creating answer" << std::endl;

    SessionContext *ctx = static_cast<SessionContext *>(user_data);
    GstPromise *answer_promise = gst_promise_new_with_change_func(on_answer_created, ctx, NULL);

    g_signal_emit_by_name(ctx->webrtcbin, "create-answer", NULL, answer_promise);
}

void WebRtcWebSocket::on_message(connection_hdl hdl, ws_server::message_ptr msg) {
    std::lock_guard<std::mutex> mutex_guard(webrtc_session_mutex);

    try {
        auto it = webrtc_sessions.find(hdl);
        if (it == webrtc_sessions.end()) {
            std::cerr << "No session context for this connection" << std::endl;
            return;
        }
        auto ctx = it->second;

        const std::string payload = msg->get_payload();
        json j = json::parse(payload);

        std::string type = j["type"].get<std::string>();

        if (type == "offer") {
            std::cout << "Received offer: " << payload << std::endl;

            std::string sdp = j["sdp"].get<std::string>();
            GstSDPMessage *sdp_message = nullptr;
            if (gst_sdp_message_new_from_text(sdp.c_str(), &sdp_message) != GST_SDP_OK) {
                g_printerr("Failed to parse SDP offer\n");
                return;
            }

            GstWebRTCSessionDescription *offer =
                gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp_message);

            GstPromise *promise =
                gst_promise_new_with_change_func(on_set_remote_description, ctx.get(), NULL);
            g_signal_emit_by_name(ctx->webrtcbin, "set-remote-description", offer, promise);
            gst_webrtc_session_description_free(offer);

            std::cout << "Setting remote description" << std::endl;
        } else if (type == "candidate") {
            std::cout << "Received ICE candidate: " << payload << std::endl;

            auto ice = j["ice"];
            std::string candidate = ice["candidate"].get<std::string>();
            guint sdpMLineIndex = static_cast<guint>(ice["sdpMLineIndex"].get<int>());

            g_signal_emit_by_name(
                ctx->webrtcbin, "add-ice-candidate", sdpMLineIndex, candidate.c_str());

            std::cout << "Added ICE candidate" << std::endl;
        }
    } catch (const std::exception &e) {
        std::cerr << "on_message exception: " << e.what() << std::endl;
    }
}
