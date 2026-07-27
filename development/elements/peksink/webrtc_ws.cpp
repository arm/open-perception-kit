/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "gst/gstpromise.h"
#include <glib-object.h>
#include <glib.h>
#include <gst/gstbin.h>
#include <gst/gstobject.h>
#include <gst/gstpad.h>
#include <gst/gstutils.h>
#include <gst/sdp/sdp.h>

#include <cctype>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// WebRTC in GST is unstable: this macro disables the warning
#define GST_USE_UNSTABLE_API

#include "Log.h"
#include "peksink.h"
#include "utils.h"
#include "webrtc_ws.h"

using namespace nlohmann;

// forward decl's
static void on_negotiation_needed(GstElement *webrtc, gpointer user_data);
static void
on_ice_candidate(GstElement *webrtc, guint mlineindex, gchar *candidate, gpointer user_data);
static void on_set_remote_description(GstPromise *promise, gpointer user_data);

// Leave enough room for SRTP, UDP and IP headers on VPN and TURN paths whose
// MTU can be lower than Ethernet's 1500 bytes (for example WSL mirrored mode).
constexpr guint kWebRtcRtpMtu = 1300;

// SessionContext is private to this compilation unit
struct SessionContext : PekSinkWebRtcSession {
    _GstPekSink *self = nullptr;
    WebRtcWebSocket *owner = nullptr;

    connection_hdl hdl;

    // aliases to make ws_server reachable from session negotiation functions
    std::shared_ptr<ws_server> ws;

    bool offer_received = false;

    SessionContext() = default;
    SessionContext(_GstPekSink *self_, WebRtcWebSocket *owner_)
        : PekSinkWebRtcSession(GST_ELEMENT(self_), self_->tee, self_->atee), self(self_),
          owner(owner_) {}

    SessionContext(const SessionContext &) = delete;
    SessionContext &operator=(const SessionContext &) = delete;

    SessionContext(SessionContext &&rhs) = delete;
    SessionContext &operator=(SessionContext &&) = delete;
};

using SessionWeakPtr = std::weak_ptr<SessionContext>;

static gpointer make_session_callback_data(const std::shared_ptr<SessionContext> &ctx) {
    return new SessionWeakPtr(ctx);
}

static void destroy_session_callback_data(gpointer user_data) {
    delete static_cast<SessionWeakPtr *>(user_data);
}

static void destroy_signal_session_callback_data(gpointer user_data, GClosure *) {
    destroy_session_callback_data(user_data);
}

static std::shared_ptr<SessionContext> lock_session_callback_data(gpointer user_data) {
    auto *weak_ctx = static_cast<SessionWeakPtr *>(user_data);
    if (!weak_ctx) {
        return nullptr;
    }
    return weak_ctx->lock();
}

WebRtcSockerError WebRtcWebSocket::setup() {
    stopping = false;
    ws = std::make_shared<ws_server>();

    ws->init_asio();

    ws->set_open_handler([this](const connection_hdl &hdl) { on_open(hdl); });
    ws->set_close_handler([this](const connection_hdl &hdl) { on_close(hdl); });
    ws->set_message_handler([this](const connection_hdl &hdl, const ws_server::message_ptr &msg) {
        on_message(hdl, msg);
    });

    ws->set_reuse_addr(true);
    ws->listen(self_->ws_port);
    ws->start_accept();

    pek::log::debug("WebSocket server started");

    return WebRtcSockerError::OK;
}

WebRtcSockerError WebRtcWebSocket::start() {
    using namespace std::chrono_literals;

    if (auto error = setup(); error != WebRtcSockerError::OK) {
        return error;
    }
    pek::log::debug("WebSocket++ server listening on port {}", self_->ws_port);

    ws_server_thread = std::thread(&ws_server::run, ws);
    while (!ws->is_listening()) {
        std::this_thread::sleep_for(1ms);
    }

    return WebRtcSockerError::OK;
}

WebRtcSockerError WebRtcWebSocket::stop() {
    stopping = true;

    std::vector<std::shared_ptr<SessionContext>> sessions;
    {
        std::lock_guard<std::mutex> mutex_guard(webrtc_session_mutex);
        for (auto &[hdl, ctx] : webrtc_sessions) {
            sessions.push_back(ctx);
        }
        webrtc_sessions.clear();
    }

    for (auto &ctx : sessions) {
        if (ctx) {
            pek::log::debug("Cleaning WebRTC session during stop");
            ctx->cleanup();
        }
    }

    if (ws) {
        ws->stop();
    }

    if (ws_server_thread.joinable()) {
        ws_server_thread.join();
    }

    return WebRtcSockerError::OK;
}

std::shared_ptr<SessionContext> WebRtcWebSocket::get_session(const connection_hdl &hdl) {
    std::lock_guard<std::mutex> mutex_guard(webrtc_session_mutex);
    auto it = webrtc_sessions.find(hdl);
    if (it == webrtc_sessions.end()) {
        return nullptr;
    }
    return it->second;
}

bool WebRtcWebSocket::cleanup_session(const connection_hdl &hdl, const char *reason) {
    std::shared_ptr<SessionContext> ctx;
    {
        std::lock_guard<std::mutex> mutex_guard(webrtc_session_mutex);
        auto it = webrtc_sessions.find(hdl);
        if (it == webrtc_sessions.end()) {
            return false;
        }
        ctx = it->second;
        webrtc_sessions.erase(it);
    }

    if (ctx) {
        pek::log::debug("Cleaning WebRTC session: {}", reason ? reason : "unknown");
        ctx->cleanup();
    }
    return true;
}

std::size_t WebRtcWebSocket::active_session_count() const {
    std::lock_guard<std::mutex> mutex_guard(webrtc_session_mutex);
    return webrtc_sessions.size();
}

void WebRtcWebSocket::set_video_pt(SessionContext *ctx) {

    pek::log::debug("SET VIDEO PT: {}", ctx->pt_video_vp8);

    auto caps_str =
        std::format("application/x-rtp,media=video,encoding-name=VP8,clock-rate=90000,payload={}",
                    ctx->pt_video_vp8);

    auto vcaps = gst_caps_from_string(caps_str.c_str());
    g_object_set(ctx->v_capsfilter, "caps", vcaps, nullptr);
    gst_caps_unref(vcaps);

    g_object_set(ctx->v_pay, "pt", (uint32_t)ctx->pt_video_vp8, nullptr);
}

bool WebRtcWebSocket::attach_video(SessionContext *ctx) {

    auto self = ctx->self;

    try {

        ctx->queue = gst_element_factory_make("queue", nullptr);
        ctx->v_pay = gst_element_factory_make("rtpvp8pay", nullptr);
        ctx->v_capsfilter = gst_element_factory_make("capsfilter", nullptr);

        if (!ctx->queue || !ctx->v_pay || !ctx->v_capsfilter) {
            throw std::runtime_error("Can't create video elements");
        }

        g_object_set(ctx->queue,
                     "leaky",
                     2, // downstream
                     "max-size-buffers",
                     10,
                     "max-size-time",
                     0,
                     "max-size-bytes",
                     0,
                     nullptr);
        g_object_set(ctx->v_pay,
                     "picture-id-mode",
                     2, // picture-id-mode = 15-bit
                     "mtu",
                     kWebRtcRtpMtu,
                     nullptr);

        set_video_pt(ctx);

        gst_bin_add_many(GST_BIN(self), ctx->queue, ctx->v_pay, ctx->v_capsfilter, nullptr);

        // queue -> pay -> caps -> webrtcbin
        if (!gst_element_link_many(ctx->queue, ctx->v_pay, ctx->v_capsfilter, nullptr)) {
            throw std::runtime_error("Can't link video queue->payload->capsfilert");
        }

        ctx->webrtc_sink_pad = gst_element_request_pad_simple(ctx->webrtcbin, "sink_%u");
        if (!ctx->webrtc_sink_pad) {
            throw std::runtime_error("Failed to request webrtc video sink pad");
        }

        auto v_pay_src = gst_element_get_static_pad(ctx->v_capsfilter, "src");
        if (!v_pay_src) {
            throw std::runtime_error("Can't get pad from video capsfilter");
        }

        if (gst_pad_link(v_pay_src, ctx->webrtc_sink_pad) != GST_PAD_LINK_OK) {
            gst_object_unref(v_pay_src);
            throw std::runtime_error("Can't link rtpv8pay to webrtc");
        };
        gst_object_unref(v_pay_src);

        // sync state
        if (!gst_element_sync_state_with_parent(ctx->queue) ||
            !gst_element_sync_state_with_parent(ctx->v_pay) ||
            !gst_element_sync_state_with_parent(ctx->v_capsfilter) ||
            !gst_element_sync_state_with_parent(ctx->webrtcbin)) {
            throw std::runtime_error("Cannot sync video elements");
        }

        pek::log::debug("Video branch attached (VP8 PT={})", ctx->pt_video_vp8);

        return true;
    } catch (const std::exception &err) {
        pek::log::debug("attach_video() failed: {}", err.what());

        ctx->cleanup();

        return false;
    }
}

gboolean WebRtcWebSocket::set_audio_pt(SessionContext *ctx) {

    pek::log::debug("SET AUDIO PT: {}", ctx->pt_audio_opus);

    auto caps_str =
        std::format("application/x-rtp,media=audio,encoding-name=OPUS,clock-rate=48000,payload={}",
                    ctx->pt_audio_opus);
    auto acaps = gst_caps_from_string(caps_str.c_str());
    g_object_set(ctx->a_capsfilter, "caps", acaps, nullptr);
    gst_caps_unref(acaps);

    // Set pt to what the browser offered (Firefox often uses 109, Chrome often 111)
    g_object_set(ctx->a_pay, "pt", ctx->pt_audio_opus, nullptr);

    return G_SOURCE_REMOVE;
}

bool WebRtcWebSocket::attach_audio(SessionContext *ctx) {

    auto self = ctx->self;

    try {
        // GST_IS_ELEMENT() is a macro performing a type check with no side effects
        if (!self->atee || !GST_IS_ELEMENT(self->atee)) { // NOSONAR
            throw std::runtime_error("Audio tee not present; cannot attach audio");
        }

        ctx->audio_queue = gst_element_factory_make("queue", nullptr);
        ctx->a_pay = gst_element_factory_make("rtpopuspay", nullptr);
        ctx->a_capsfilter = gst_element_factory_make("capsfilter", nullptr);

        if (!ctx->audio_queue || !ctx->a_pay || !ctx->a_capsfilter) {
            throw std::runtime_error("Failed to create audio elements");
        }

        set_audio_pt(ctx);

        g_object_set(ctx->audio_queue,
                     "leaky",
                     2,
                     "max-size-buffers",
                     10,
                     "max-size-time",
                     0,
                     "max-size-bytes",
                     0,
                     nullptr);

        // Add to the parent bin (PekSink bin)
        gst_bin_add_many(GST_BIN(self), ctx->audio_queue, ctx->a_pay, ctx->a_capsfilter, nullptr);

        // audio_queue -> pay -> caps
        if (!gst_element_link_many(ctx->audio_queue, ctx->a_pay, ctx->a_capsfilter, nullptr)) {
            throw std::runtime_error("Failed to link audio_queue -> pay -> caps");
        }

        // Link capsfilter -> webrtcbin request sink pad
        ctx->audio_webrtc_sink_pad = gst_element_request_pad_simple(ctx->webrtcbin, "sink_%u");
        if (!ctx->audio_webrtc_sink_pad) {
            throw std::runtime_error("Failed to request webrtc audio sink pad");
        }

        auto a_pay_src = gst_element_get_static_pad(ctx->a_capsfilter, "src");
        if (!a_pay_src) {
            throw std::runtime_error("Can't get pad from audio capsfilter");
        }

        if (gst_pad_link(a_pay_src, ctx->audio_webrtc_sink_pad) != GST_PAD_LINK_OK) {
            gst_object_unref(a_pay_src);
            throw std::runtime_error("Can't link rtpopuspay to webrtc");
        };
        gst_object_unref(a_pay_src);

        // Sync state
        if (!gst_element_sync_state_with_parent(ctx->audio_queue) ||
            !gst_element_sync_state_with_parent(ctx->a_pay) ||
            !gst_element_sync_state_with_parent(ctx->a_capsfilter)) {
            throw std::runtime_error("Failed to sync audio elements");
        }

        pek::log::debug("Audio branch attached (Opus PT={})", ctx->pt_audio_opus);

        return true;
    } catch (const std::exception &err) {
        pek::log::debug("attach_audio() failed: {}", err.what());

        ctx->cleanup();

        return false;
    }
}

void WebRtcWebSocket::on_open(const connection_hdl &hdl) {

    pek::log::debug("WebSocket connection opened");
    if (stopping) {
        pek::log::debug("Ignoring WebSocket open while stopping");
        return;
    }

    cleanup_session(hdl, "replaced by new open event");

    auto ctx = std::make_shared<SessionContext>(self_, this);
    ctx->ws = ws;
    ctx->hdl = hdl;

    // ---- Per-client elements ----
    ctx->webrtcbin = gst_element_factory_make("webrtcbin", nullptr);
    if (!ctx->webrtcbin) {
        pek::log::debug("Failed to create per-client webrtcbin");
        return;
    }

    g_object_set(ctx->webrtcbin, "latency", 200u, "reuse-source-pads", FALSE, nullptr);
    if (self_->webrtc_stun_server && self_->webrtc_stun_server[0] != '\0') {
        g_object_set(ctx->webrtcbin, "stun-server", self_->webrtc_stun_server, nullptr);
    }
    if (self_->webrtc_turn_server && self_->webrtc_turn_server[0] != '\0') {
        g_object_set(ctx->webrtcbin, "turn-server", self_->webrtc_turn_server, nullptr);
    }

    // WebRTC callbacks (per client webrtcbin!)
    ctx->onn_id = g_signal_connect_data(ctx->webrtcbin,
                                        "on-negotiation-needed",
                                        G_CALLBACK(on_negotiation_needed),
                                        make_session_callback_data(ctx),
                                        destroy_signal_session_callback_data,
                                        GConnectFlags(0));
    ctx->oic_id = g_signal_connect_data(ctx->webrtcbin,
                                        "on-ice-candidate",
                                        G_CALLBACK(on_ice_candidate),
                                        make_session_callback_data(ctx),
                                        destroy_signal_session_callback_data,
                                        GConnectFlags(0));

    // Add to peksink bin
    gst_bin_add_many(GST_BIN(self_), ctx->webrtcbin, nullptr);

    if (!attach_video(ctx.get()) || !attach_audio(ctx.get())) {
        ctx->cleanup();
        return;
    }

    dump_sink_pads(ctx->webrtcbin);

    std::lock_guard<std::mutex> g(webrtc_session_mutex);
    webrtc_sessions[hdl] = ctx;
}

void WebRtcWebSocket::on_close(const connection_hdl &hdl) {
    pek::log::debug("WebSocket connection closed");
    if (cleanup_session(hdl, "websocket close")) {
        dump_pipeline_graph(GST_ELEMENT(self_), "pipeline_on_close");
    }
}

/*
 * IMPORTANT:
 * Do NOT connect tee → (per-client elements) before payload types are set from the offer.
 * webrtcbin snapshots RTP properties on first buffer.
 */
// this method links the per-client (both the audio and video) elements to the main graph
bool WebRtcWebSocket::link_per_client_elements(SessionContext *ctx) {

    GstPad *q_sink = nullptr;
    GstPad *aq_sink = nullptr;
    try {
        if (ctx->tee_src_pad || ctx->audio_tee_src_pad) {
            throw std::runtime_error("Per-client tee pads already linked");
        }

        // create the tee source pad
        ctx->tee_src_pad = gst_element_request_pad_simple(ctx->self->tee, "src_%u");
        if (!ctx->tee_src_pad) {
            pek::log::debug("Failed to request video src pad from tee");
            return false;
        }

        q_sink = gst_element_get_static_pad(ctx->queue, "sink");
        if (!ctx->tee_src_pad || !q_sink ||
            gst_pad_link(ctx->tee_src_pad, q_sink) != GST_PAD_LINK_OK) {
            throw std::runtime_error("Failed to link queue to video tee");
        }
        gst_object_unref(q_sink);
        q_sink = nullptr;

        // atee -> audio_queue (pad link)
        ctx->audio_tee_src_pad = gst_element_request_pad_simple(ctx->self->atee, "src_%u");
        if (!ctx->audio_tee_src_pad) {
            throw std::runtime_error("Failed to request audio src pad from atee");
        }

        aq_sink = gst_element_get_static_pad(ctx->audio_queue, "sink");
        if (!aq_sink || gst_pad_link(ctx->audio_tee_src_pad, aq_sink) != GST_PAD_LINK_OK) {
            throw std::runtime_error("Failed to link atee -> audio_queue");
        }
        gst_object_unref(aq_sink);
        aq_sink = nullptr;

        return true;

    } catch (const std::exception &e) {
        pek::log::debug("link_per_client_elements failed: {}", e.what());

        if (q_sink)
            gst_object_unref(q_sink);
        if (aq_sink)
            gst_object_unref(aq_sink);
        release_request_pad_and_unref(self_->tee, &ctx->tee_src_pad);
        release_request_pad_and_unref(self_->atee, &ctx->audio_tee_src_pad);
        return false;
    }
}

void WebRtcWebSocket::process_offer(const std::shared_ptr<SessionContext> &ctx, const json &jsn) {

    if (ctx->offer_received) {
        pek::log::debug("Repeated offer received for the same WebSocket handle");
        cleanup_session(ctx->hdl, "repeated offer");
        return;
    }
    ctx->offer_received = true;

    auto sdp = jsn["sdp"].get<std::string>();
    GstSDPMessage *sdp_message = nullptr;
    if (gst_sdp_message_new_from_text(sdp.c_str(), &sdp_message) != GST_SDP_OK) {
        pek::log::debug("Failed to parse SDP offer");
        cleanup_session(ctx->hdl, "invalid offer sdp");
        return;
    }

    ctx->pt_video_vp8 = find_pt_for_codec(sdp_message, "video", "VP8");
    ctx->pt_audio_opus = find_pt_for_codec(sdp_message, "audio", "opus");
    if (ctx->pt_video_vp8 < 0 || ctx->pt_audio_opus < 0) {
        pek::log::debug("Offer is missing required payload types: VP8={}, opus={}",
                        ctx->pt_video_vp8,
                        ctx->pt_audio_opus);
        gst_sdp_message_free(sdp_message);
        cleanup_session(ctx->hdl, "unsupported offer payload types");
        return;
    }

    set_video_pt(ctx.get());
    set_audio_pt(ctx.get());

    if (!link_per_client_elements(ctx.get())) {
        gst_sdp_message_free(sdp_message);
        cleanup_session(ctx->hdl, "per-client link failed");
        return;
    }

    if (!gst_element_sync_state_with_parent(ctx->webrtcbin)) {
        gst_sdp_message_free(sdp_message);
        cleanup_session(ctx->hdl, "webrtcbin state sync failed");
        return;
    }

    pek::log::debug("Offer PTs: VP8={}, opus={}", ctx->pt_video_vp8, ctx->pt_audio_opus);

    auto offer = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp_message);
    auto promise = gst_promise_new_with_change_func(
        on_set_remote_description, make_session_callback_data(ctx), destroy_session_callback_data);

    g_signal_emit_by_name(ctx->webrtcbin, "set-remote-description", offer, promise);
    gst_webrtc_session_description_free(offer);

    pek::log::debug("Setting remote description");
}

void WebRtcWebSocket::process_canditate(const std::shared_ptr<SessionContext> &ctx,
                                        const json &jsn) {
    pek::log::debug("Received ICE candidate");

    auto ice = jsn["ice"];
    auto candidate = ice["candidate"].get<std::string>();
    auto sdpMLineIndex = static_cast<guint>(ice["sdpMLineIndex"].get<int>());

    g_signal_emit_by_name(ctx->webrtcbin, "add-ice-candidate", sdpMLineIndex, candidate.c_str());

    pek::log::debug("Added ICE candidate: candidate={} mlindex={}", candidate, sdpMLineIndex);
}

void WebRtcWebSocket::on_message(const connection_hdl &hdl, const ws_server::message_ptr &msg) {
    pek::log::debug("on_message");

    if (stopping) {
        return;
    }

    auto ctx = get_session(hdl);
    if (!ctx) {
        pek::log::debug("No session context for this connection");
        return;
    }

    try {
        const std::string payload = msg->get_payload();
        json jsn = json::parse(payload);

        auto type = jsn["type"].get<std::string>();

        if (type == "offer") {
            process_offer(ctx, jsn);
        } else if (type == "candidate") {
            process_canditate(ctx, jsn);
        }

    } catch (const std::exception &e) {
        pek::log::debug("on_message exception: {}", e.what());
        cleanup_session(hdl, "message handling failure");
    }
}

static bool iequals_prefix(const std::string &s, const std::string &p) {
    if (s.size() < p.size())
        return false;
    for (size_t i = 0; i < p.size(); ++i) {
        if (std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)p[i]))
            return false;
    }
    return true;
}

// Returns -1 if not found
int WebRtcWebSocket::find_pt_for_codec(const GstSDPMessage *msg,
                                       const char *media_type, // "video" or "audio"
                                       const char *codec_name) // "VP8" or "opus"
{
    const auto n_media = gst_sdp_message_medias_len(msg);
    for (guint mi = 0; mi < n_media; ++mi) {
        const auto m = gst_sdp_message_get_media(msg, mi);
        if (!m)
            continue;

        const auto mt = gst_sdp_media_get_media(m); // "audio"/"video"
        if (!mt || g_strcmp0(mt, media_type) != 0)
            continue;

        const auto n_attr = gst_sdp_media_attributes_len(m);
        for (guint ai = 0; ai < n_attr; ++ai) {
            const auto a = gst_sdp_media_get_attribute(m, ai);
            if (!a || !a->key || !a->value)
                continue;

            // In GStreamer, rtpmap appears as key="rtpmap" value="<pt> <codec>/<clock>[/ch]"
            if (g_strcmp0(a->key, "rtpmap") != 0)
                continue;

            auto v = std::string(a->value); // e.g. "120 VP8/90000" or "109 opus/48000/2"

            // Split: "<pt> <rest>"
            const auto sp = v.find(' ');
            if (sp == std::string::npos)
                continue;

            const auto pt_str = v.substr(0, sp);
            const auto rest = v.substr(sp + 1);

            // Compare codec prefix case-insensitively: "VP8/..." or "opus/..."
            auto want = std::string(codec_name);
            want.push_back('/');

            if (!iequals_prefix(rest, want))
                continue;

            try {
                return std::stoi(pt_str);
            } catch (...) {
                continue;
            }
        }
    }
    return -1;
}

//
// Pure C functions - GstWebRTC callbacks
//
static void on_negotiation_needed(GstElement *webrtc, gpointer user_data) {
    pek::log::debug("Negotiation needed");
}

static bool send_text(const std::shared_ptr<SessionContext> &ctx, const std::string &text) {
    if (!ctx || !ctx->ws || ctx->cleaned_up()) {
        return false;
    }

    try {
        ctx->ws->send(ctx->hdl, text, websocketpp::frame::opcode::text);
        return true;
    } catch (const websocketpp::exception &e) {
        pek::log::debug("WebSocket send error: {}", e.what());
        return false;
    }
}

static void send_ice_candidate_message(const std::shared_ptr<SessionContext> &ctx,
                                       guint mlineindex,
                                       const gchar *candidate) {
    pek::log::debug("Sending ICE candidate: mlineindex={}, candidate={}", mlineindex, candidate);

    json msg;
    msg["type"] = "candidate";
    msg["ice"] = {{"candidate", candidate}, {"sdpMLineIndex", mlineindex}};

    if (!send_text(ctx, msg.dump())) {
        auto owner = ctx->owner;
        auto hdl = ctx->hdl;
        if (owner) {
            owner->cleanup_session(hdl, "ice candidate send failure");
        }
        return;
    }

    pek::log::debug("ICE candidate sent");
}

static void
on_ice_candidate(GstElement *webrtc, guint mlineindex, gchar *candidate, gpointer user_data) {
    pek::log::debug("on_ice_candidate");

    auto ctx = lock_session_callback_data(user_data);
    if (!ctx || ctx->cleaned_up()) {
        pek::log::debug("Ignoring ICE candidate for cleaned-up session");
        return;
    }

    if (!candidate || candidate[0] == '\0') {
        pek::log::debug("Sending ICE end-of-candidates for mline {}", mlineindex);
        send_ice_candidate_message(ctx, mlineindex, "");
        return;
    }

    pek::log::debug("ICE candidate generated: mlineindex={} candidate={}", mlineindex, candidate);

    send_ice_candidate_message(ctx, mlineindex, candidate);
}

static void on_answer_created(GstPromise *promise, gpointer user_data) {
    pek::log::debug("on_answer_created");

    auto ctx = lock_session_callback_data(user_data);
    if (!ctx || ctx->cleaned_up()) {
        pek::log::debug("Ignoring answer for cleaned-up session");
        gst_promise_unref(promise);
        return;
    }

    GstWebRTCSessionDescription *answer = nullptr;
    const GstStructure *reply = gst_promise_get_reply(promise);
    if (!reply ||
        !gst_structure_get(
            reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, nullptr) ||
        !answer) {
        pek::log::debug("No answer in promise reply");
        auto owner = ctx->owner;
        auto hdl = ctx->hdl;
        gst_promise_unref(promise);
        if (owner) {
            owner->cleanup_session(hdl, "answer creation failure");
        }
        return;
    }

    auto local_promise = gst_promise_new();
    g_signal_emit_by_name(ctx->webrtcbin, "set-local-description", answer, local_promise);
    gst_promise_unref(local_promise);

    json sdp_json;
    auto sdp_text = gst_sdp_message_as_text(answer->sdp);
    sdp_json["type"] = "answer";
    sdp_json["sdp"] = sdp_text ? sdp_text : "";
    const bool sent = send_text(ctx, sdp_json.dump());
    g_free(sdp_text);

    auto owner = ctx->owner;
    auto hdl = ctx->hdl;
    gst_webrtc_session_description_free(answer);
    gst_promise_unref(promise);

    if (!sent && owner) {
        owner->cleanup_session(hdl, "answer send failure");
    }
}

static void on_set_remote_description(GstPromise *promise, gpointer user_data) {
    pek::log::debug("on_set_remote_description");

    auto ctx = lock_session_callback_data(user_data);
    if (!ctx || ctx->cleaned_up()) {
        pek::log::debug("Ignoring remote description for cleaned-up session");
        gst_promise_unref(promise);
        return;
    }

    auto answer_promise = gst_promise_new_with_change_func(
        on_answer_created, make_session_callback_data(ctx), destroy_session_callback_data);

    g_signal_emit_by_name(ctx->webrtcbin, "create-answer", nullptr, answer_promise);

    gst_promise_unref(promise);
}
