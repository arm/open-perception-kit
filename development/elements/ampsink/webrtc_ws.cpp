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
    if (!candidate || candidate[0] == '\0') {
        std::cout << "ICE end-of-candidates for mline " << mlineindex << " (not sending)\n";
        return;
    }

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

    // ---- Per-client elements ----
    ctx->queue = gst_element_factory_make("queue", nullptr);
    ctx->webrtcbin = gst_element_factory_make("webrtcbin", nullptr);
    ctx->v_capsfilter = gst_element_factory_make("capsfilter", nullptr);

    if (!ctx->queue || !ctx->webrtcbin || !ctx->v_capsfilter) {
        std::cerr << "Failed to create per-client elements (queue/webrtcbin/v_capsfilter)\n";
        if (ctx->queue)
            gst_object_unref(ctx->queue);
        if (ctx->webrtcbin)
            gst_object_unref(ctx->webrtcbin);
        if (ctx->v_capsfilter)
            gst_object_unref(ctx->v_capsfilter);
        return;
    }

    g_object_set(ctx->webrtcbin,
                 "stun-server",
                 STUN_SERVER,
                 "latency",
                 200u,
                 "reuse-source-pads",
                 FALSE,
                 nullptr);

    // Force video RTP caps so webrtcbin can route to the correct m-line/transceiver.
    // TODO@ibori: does not work when Firefox is used
    {
        GstCaps *vcaps = gst_caps_from_string(
            "application/x-rtp,media=video,encoding-name=VP8,clock-rate=90000,payload=96");
        g_object_set(ctx->v_capsfilter, "caps", vcaps, NULL);
        gst_caps_unref(vcaps);
    }

    // Add to ampsink bin
    gst_bin_add_many(GST_BIN(self_), ctx->queue, ctx->v_capsfilter, ctx->webrtcbin, nullptr);

    // ---- VIDEO: tee -> queue ----
    ctx->tee_src_pad = gst_element_request_pad_simple(self_->tee, "src_%u");
    if (!ctx->tee_src_pad) {
        std::cerr << "Failed to request video src pad from tee\n";
        gst_bin_remove_many(GST_BIN(self_), ctx->queue, ctx->v_capsfilter, ctx->webrtcbin, nullptr);
        return;
    }

    GstPad *queue_sink_pad = gst_element_get_static_pad(ctx->queue, "sink");
    if (!queue_sink_pad) {
        std::cerr << "Failed to get sink pad of client video queue\n";
        gst_element_release_request_pad(self_->tee, ctx->tee_src_pad);
        gst_object_unref(ctx->tee_src_pad);
        ctx->tee_src_pad = nullptr;

        gst_bin_remove_many(GST_BIN(self_), ctx->queue, ctx->v_capsfilter, ctx->webrtcbin, nullptr);
        return;
    }

    if (gst_pad_link(ctx->tee_src_pad, queue_sink_pad) != GST_PAD_LINK_OK) {
        std::cerr << "Failed to link tee -> client video queue\n";
        gst_object_unref(queue_sink_pad);

        gst_element_release_request_pad(self_->tee, ctx->tee_src_pad);
        gst_object_unref(ctx->tee_src_pad);
        ctx->tee_src_pad = nullptr;

        gst_bin_remove_many(GST_BIN(self_), ctx->queue, ctx->v_capsfilter, ctx->webrtcbin, nullptr);
        return;
    }
    gst_object_unref(queue_sink_pad);

    // ---- VIDEO: queue -> capsfilter -> webrtcbin ----
    if (!gst_element_link_many(ctx->queue, ctx->v_capsfilter, ctx->webrtcbin, nullptr)) {
        std::cerr << "Failed to link queue -> v_capsfilter -> webrtcbin\n";

        // Undo tee -> queue
        GstPad *qs = gst_element_get_static_pad(ctx->queue, "sink");
        if (qs) {
            gst_pad_unlink(ctx->tee_src_pad, qs);
            gst_object_unref(qs);
        }
        gst_element_release_request_pad(self_->tee, ctx->tee_src_pad);
        gst_object_unref(ctx->tee_src_pad);
        ctx->tee_src_pad = nullptr;

        gst_bin_remove_many(GST_BIN(self_), ctx->queue, ctx->v_capsfilter, ctx->webrtcbin, nullptr);
        return;
    }

    // Sync to parent state
    gst_element_sync_state_with_parent(ctx->queue);
    gst_element_sync_state_with_parent(ctx->v_capsfilter);
    gst_element_sync_state_with_parent(ctx->webrtcbin);

    // WebRTC callbacks (per client webrtcbin!)
    g_signal_connect(
        ctx->webrtcbin, "on-negotiation-needed", G_CALLBACK(on_negotiation_needed), ctx.get());
    g_signal_connect(ctx->webrtcbin, "on-ice-candidate", G_CALLBACK(on_ice_candidate), ctx.get());

    // Store session early
    {
        std::lock_guard<std::mutex> g(webrtc_session_mutex);
        webrtc_sessions[hdl] = ctx;
    }

    // ========================= OPTIONAL AUDIO =========================
    if (!self_->atee || !GST_IS_ELEMENT(self_->atee)) {
        std::cout << "Audio tee not present; continuing video-only\n";
        return;
    }

    ctx->audio_queue = gst_element_factory_make("queue", nullptr);
    ctx->a_capsfilter = gst_element_factory_make("capsfilter", nullptr);

    if (!ctx->audio_queue || !ctx->a_capsfilter) {
        std::cerr << "Failed to create audio_queue/a_capsfilter; continuing video-only\n";
        if (ctx->audio_queue)
            gst_object_unref(ctx->audio_queue);
        if (ctx->a_capsfilter)
            gst_object_unref(ctx->a_capsfilter);
        ctx->audio_queue = nullptr;
        ctx->a_capsfilter = nullptr;
        return;
    }

    // TODO@ibori: bad when firefox is used
    {
        GstCaps *acaps = gst_caps_from_string(
            "application/x-rtp,media=audio,encoding-name=OPUS,clock-rate=48000,payload=111");
        g_object_set(ctx->a_capsfilter, "caps", acaps, NULL);
        gst_caps_unref(acaps);
    }

    gst_bin_add_many(GST_BIN(self_), ctx->audio_queue, ctx->a_capsfilter, nullptr);
    gst_element_sync_state_with_parent(ctx->audio_queue);
    gst_element_sync_state_with_parent(ctx->a_capsfilter);

    // atee -> audio_queue
    ctx->audio_tee_src_pad = gst_element_request_pad_simple(self_->atee, "src_%u");
    if (!ctx->audio_tee_src_pad) {
        std::cerr << "Failed to request audio src pad from atee; continuing video-only\n";
        gst_bin_remove_many(GST_BIN(self_), ctx->audio_queue, ctx->a_capsfilter, nullptr);
        ctx->audio_queue = nullptr;
        ctx->a_capsfilter = nullptr;
        return;
    }

    GstPad *aq_sink = gst_element_get_static_pad(ctx->audio_queue, "sink");
    if (!aq_sink || gst_pad_link(ctx->audio_tee_src_pad, aq_sink) != GST_PAD_LINK_OK) {
        std::cerr << "Failed to link atee -> audio_queue; continuing video-only\n";
        if (aq_sink)
            gst_object_unref(aq_sink);

        gst_element_release_request_pad(self_->atee, ctx->audio_tee_src_pad);
        gst_object_unref(ctx->audio_tee_src_pad);
        ctx->audio_tee_src_pad = nullptr;

        gst_bin_remove_many(GST_BIN(self_), ctx->audio_queue, ctx->a_capsfilter, nullptr);
        ctx->audio_queue = nullptr;
        ctx->a_capsfilter = nullptr;
        return;
    }
    gst_object_unref(aq_sink);

    // audio_queue -> capsfilter -> webrtcbin
    if (!gst_element_link_many(ctx->audio_queue, ctx->a_capsfilter, ctx->webrtcbin, nullptr)) {
        std::cerr
            << "Failed to link audio_queue -> a_capsfilter -> webrtcbin; continuing video-only\n";

        // undo atee -> audio_queue
        GstPad *aqs = gst_element_get_static_pad(ctx->audio_queue, "sink");
        if (aqs) {
            gst_pad_unlink(ctx->audio_tee_src_pad, aqs);
            gst_object_unref(aqs);
        }
        gst_element_release_request_pad(self_->atee, ctx->audio_tee_src_pad);
        gst_object_unref(ctx->audio_tee_src_pad);
        ctx->audio_tee_src_pad = nullptr;

        gst_bin_remove_many(GST_BIN(self_), ctx->audio_queue, ctx->a_capsfilter, nullptr);
        ctx->audio_queue = nullptr;
        ctx->a_capsfilter = nullptr;
        return;
    }

    gst_element_sync_state_with_parent(ctx->audio_queue);
    gst_element_sync_state_with_parent(ctx->a_capsfilter);

    std::cout << "Audio branch attached for this client\n";
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
    if (ctx->audio_queue)
        gst_element_set_state(ctx->audio_queue, GST_STATE_NULL);

    // 2) Unlink VIDEO tee -> client queue and release the tee src pad
    if (ctx->tee_src_pad) {
        GstPad *queue_sink = ctx->queue ? gst_element_get_static_pad(ctx->queue, "sink") : nullptr;
        if (queue_sink) {
            gst_pad_unlink(ctx->tee_src_pad, queue_sink);
            gst_object_unref(queue_sink);
        }

        gst_element_release_request_pad(self_->tee, ctx->tee_src_pad);
        gst_object_unref(ctx->tee_src_pad);
        ctx->tee_src_pad = nullptr;
    }

    // 2b) Unlink AUDIO tee -> audio_queue and release the audio tee src pad
    if (ctx->audio_tee_src_pad) {
        GstPad *aq_sink =
            ctx->audio_queue ? gst_element_get_static_pad(ctx->audio_queue, "sink") : nullptr;
        if (aq_sink) {
            gst_pad_unlink(ctx->audio_tee_src_pad, aq_sink);
            gst_object_unref(aq_sink);
        }

        // Only if we have an audio tee in the bin
        if (self_->atee) {
            gst_element_release_request_pad(self_->atee, ctx->audio_tee_src_pad);
        }
        gst_object_unref(ctx->audio_tee_src_pad);
        ctx->audio_tee_src_pad = nullptr;
    }

    // 3) Release VIDEO webrtcbin sink_%u pad
    if (ctx->webrtc_sink_pad && ctx->webrtcbin) {
        gst_element_release_request_pad(ctx->webrtcbin, ctx->webrtc_sink_pad);
        gst_object_unref(ctx->webrtc_sink_pad);
        ctx->webrtc_sink_pad = nullptr;
    }

    // 3b) Release AUDIO webrtcbin sink_%u pad
    if (ctx->audio_webrtc_sink_pad && ctx->webrtcbin) {
        gst_element_release_request_pad(ctx->webrtcbin, ctx->audio_webrtc_sink_pad);
        gst_object_unref(ctx->audio_webrtc_sink_pad);
        ctx->audio_webrtc_sink_pad = nullptr;
    }

    // 4) Remove per-client elements from the bin (include audio_queue)
    if (ctx->audio_queue || ctx->queue || ctx->webrtcbin) {
        // Note: gst_bin_remove_many tolerates NULLs poorly in some builds;
        gst_bin_remove_many(GST_BIN(self_), ctx->audio_queue, ctx->queue, ctx->webrtcbin, nullptr);
    }

    ctx->audio_queue = nullptr;
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
