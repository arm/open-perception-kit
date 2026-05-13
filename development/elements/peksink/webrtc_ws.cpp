/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
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

// WebRTC in GST is unstable: this macro disables the warning
#define GST_USE_UNSTABLE_API

#include "auxiliary.h"
#include "peksink.h"
#include "utils.h"
#include "webrtc_ws.h"

using namespace nlohmann;

// forward decl's
static void on_negotiation_needed(GstElement *webrtc, gpointer user_data);
static void
on_ice_candidate(GstElement *webrtc, guint mlineindex, gchar *candidate, gpointer user_data);
static void on_set_remote_description(GstPromise *promise, gpointer user_data);

#define REMOVE_AND_FREE_ELEMENT(element)                                                           \
    do {                                                                                           \
        if ((element)) {                                                                           \
            GstObject *parent = gst_object_get_parent(GST_OBJECT((element)));                      \
            if (parent) {                                                                          \
                gst_object_unref(parent);                                                          \
                gst_bin_remove(GST_BIN(self), (element));                                          \
            } else {                                                                               \
                gst_object_unref((element));                                                       \
            }                                                                                      \
            (element) = nullptr;                                                                   \
        }                                                                                          \
    } while (0)

#define FREE_PAD(pad, element)                                                                     \
    do {                                                                                           \
        if ((pad)) {                                                                               \
            if ((element) && GST_IS_ELEMENT((element)) && GST_IS_PAD((pad))) {                     \
                gst_element_release_request_pad((element), (pad));                                 \
            }                                                                                      \
            gst_object_unref((pad));                                                               \
            (pad) = nullptr;                                                                       \
        }                                                                                          \
    } while (0)

// SessionContext is private to this compilation unit
struct SessionContext {
    _GstPekSink *self;

    connection_hdl hdl;

    // aliases to make ws_server reachable from session negotiation functions
    std::shared_ptr<ws_server> ws;

    gulong onn_id;
    gulong oic_id;

    // Per-client GStreamer branch
    GstElement *webrtcbin = nullptr;
    GstPad *webrtc_sink_pad = nullptr; // requested from webrtcbin ("sink_%u")
    GstPad *audio_webrtc_sink_pad = nullptr;

    GstElement *queue = nullptr; // between tee and webrtcbin
    GstElement *audio_queue = nullptr;

    GstPad *tee_src_pad = nullptr; // requested from tee
    GstPad *audio_tee_src_pad = nullptr;

    GstElement *a_capsfilter = nullptr;
    GstElement *v_capsfilter = nullptr;

    // Puts the VP8/Opus stream to RTP payloads
    GstElement *v_pay = nullptr;
    GstElement *a_pay = nullptr;

    gint pt_video_vp8 = 96;
    gint pt_audio_opus = 111;

    SessionContext() = default;

    SessionContext(const SessionContext &) = delete;
    SessionContext &operator=(const SessionContext &) = delete;

    SessionContext(SessionContext &&rhs) = delete;
    SessionContext &operator=(SessionContext &&) = delete;

    ~SessionContext() {
        DBG("Destruct SessionContext");

        if (!self)
            return;

        // Stop elements (only if non-null)
        set_state_elements_many(
            GST_STATE_NULL,
            {webrtcbin, queue, v_pay, v_capsfilter, audio_queue, a_pay, a_capsfilter});

        // Unlink VIDEO tee -> client queue and release the tee src pad
        if (tee_src_pad) {
            auto queue_sink = queue ? gst_element_get_static_pad(queue, "sink") : nullptr;
            if (queue_sink) {
                gst_pad_unlink(tee_src_pad, queue_sink);
                gst_object_unref(queue_sink);
            }

            gst_element_release_request_pad(self->tee, tee_src_pad);
            gst_object_unref(tee_src_pad);
            tee_src_pad = nullptr;
        }

        // Unlink AUDIO tee -> audio_queue and release the audio tee src pad
        if (audio_tee_src_pad) {
            auto aq_sink = audio_queue ? gst_element_get_static_pad(audio_queue, "sink") : nullptr;
            if (aq_sink) {
                gst_pad_unlink(audio_tee_src_pad, aq_sink);
                gst_object_unref(aq_sink);
            }

            // Only if we have an audio tee in the bin
            if (self->atee) {
                gst_element_release_request_pad(self->atee, audio_tee_src_pad);
            }
            gst_object_unref(audio_tee_src_pad);
            audio_tee_src_pad = nullptr;
        }

        FREE_PAD(webrtc_sink_pad, webrtcbin);
        FREE_PAD(audio_webrtc_sink_pad, webrtcbin);

        // Remove per-client elements from the bin
        if (audio_queue || queue || webrtcbin || v_pay || a_pay || v_capsfilter || a_capsfilter) {
            // Note1: gst_bin_remove_many tolerates NULLs poorly in some builds;
            // Note2: it makes unref too?
            gst_bin_remove_many(GST_BIN(self),
                                audio_queue,
                                queue,
                                v_pay,
                                a_pay,
                                v_capsfilter,
                                a_capsfilter,
                                webrtcbin,
                                nullptr);
        }

        audio_queue = queue = webrtcbin = v_pay = a_pay = v_capsfilter = a_capsfilter = nullptr;
    }
};

WebRtcSockerError WebRtcWebSocket::setup() {
    ws = std::make_shared<ws_server>();

    ws->init_asio();

    ws->set_open_handler([this](connection_hdl hdl) { on_open(hdl); });
    ws->set_close_handler([this](connection_hdl hdl) { on_close(hdl); });
    ws->set_message_handler(
        [this](connection_hdl hdl, ws_server::message_ptr msg) { on_message(hdl, msg); });

    ws->set_reuse_addr(true);
    ws->listen(self_->ws_port);
    ws->start_accept();

    DBG("WebSocket server started");

    return WebRtcSockerError::OK;
}

WebRtcSockerError WebRtcWebSocket::start() {
    using namespace std::chrono_literals;

    if (auto error = setup(); error != WebRtcSockerError::OK) {
        return error;
    }
    DBG("WebSocket++ server listening on port {}", self_->ws_port);

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

    webrtc_sessions.clear();

    return WebRtcSockerError::OK;
}

void WebRtcWebSocket::set_video_pt(SessionContext *ctx) {

    DBG("SET VIDEO PT: {}", ctx->pt_video_vp8);

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
        g_object_set(ctx->v_pay, "picture-id-mode", 2, nullptr); // picture-id-mode = 15-bit

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

        DBG("Video branch attached (VP8 PT={})", ctx->pt_video_vp8);

        return true;
    } catch (const std::exception &err) {
        DBG("attach_video() failed: {}", err.what());

        FREE_PAD(ctx->webrtc_sink_pad, ctx->webrtcbin);
        REMOVE_AND_FREE_ELEMENT(ctx->v_capsfilter);
        REMOVE_AND_FREE_ELEMENT(ctx->v_pay);
        REMOVE_AND_FREE_ELEMENT(ctx->queue);

        return false;
    }
}

gboolean WebRtcWebSocket::set_audio_pt(SessionContext *ctx) {

    DBG("SET AUDIO PT: {}", ctx->pt_audio_opus);

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

        DBG("Audio branch attached (Opus PT={})", ctx->pt_audio_opus);

        return true;
    } catch (const std::exception &err) {
        DBG("attach_audio() failed: {}", err.what());

        FREE_PAD(ctx->audio_webrtc_sink_pad, ctx->webrtcbin);
        REMOVE_AND_FREE_ELEMENT(ctx->a_capsfilter);
        REMOVE_AND_FREE_ELEMENT(ctx->a_pay);
        REMOVE_AND_FREE_ELEMENT(ctx->audio_queue);

        return false;
    }
}

void WebRtcWebSocket::on_open(connection_hdl hdl) {

    DBG("WebSocket connection opened");

    auto ctx = std::make_shared<SessionContext>();
    ctx->self = self_;
    ctx->ws = ws;
    ctx->hdl = hdl;

    // ---- Per-client elements ----
    ctx->webrtcbin = gst_element_factory_make("webrtcbin", nullptr);
    if (!ctx->webrtcbin) {
        DBG("Failed to create per-client webrtcbin");
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

    // WebRTC callbacks (per client webrtcbin!)
    ctx->onn_id = g_signal_connect(
        ctx->webrtcbin, "on-negotiation-needed", G_CALLBACK(on_negotiation_needed), ctx.get());
    ctx->oic_id = g_signal_connect(
        ctx->webrtcbin, "on-ice-candidate", G_CALLBACK(on_ice_candidate), ctx.get());

    // Add to peksink bin
    gst_bin_add_many(GST_BIN(self_), ctx->webrtcbin, nullptr);

    attach_video(ctx.get());
    attach_audio(ctx.get());

    dump_sink_pads(ctx->webrtcbin);

    std::lock_guard<std::mutex> g(webrtc_session_mutex);
    webrtc_sessions[hdl] = ctx;
}

void WebRtcWebSocket::on_close(connection_hdl hdl) {
    DBG("WebSocket connection closed");

    std::shared_ptr<SessionContext> ctx = nullptr;
    {
        std::lock_guard<std::mutex> mutex_guard(webrtc_session_mutex);
        auto &sessions = webrtc_sessions;

        auto it = sessions.find(hdl);
        if (it == sessions.end()) {
            return;
        }

        ctx = it->second;

        g_signal_handler_disconnect(ctx->webrtcbin, ctx->onn_id);
        g_signal_handler_disconnect(ctx->webrtcbin, ctx->oic_id);

        // Forget the session
        sessions.erase(it);
    }

    if (ctx) {
        dump_sink_pads(ctx->webrtcbin);
        dump_pipeline_graph(GST_ELEMENT(ctx->self), "pipeline_on_close");
    }
}

/*
 * IMPORTANT:
 * Do NOT connect tee → (per-client elements) before payload types are set from the offer.
 * webrtcbin snapshots RTP properties on first buffer.
 */
// this method links the per-client (both the audio and video) elements to the main graph
void WebRtcWebSocket::link_per_client_elements(SessionContext *ctx) {

    GstPad *q_sink = nullptr;
    GstPad *aq_sink = nullptr;
    try {
        // create the tee source pad
        ctx->tee_src_pad = gst_element_request_pad_simple(ctx->self->tee, "src_%u");
        if (!ctx->tee_src_pad) {
            DBG("Failed to request video src pad from tee");
            gst_bin_remove_many(GST_BIN(ctx->self), ctx->queue, ctx->webrtcbin, nullptr);
            return;
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

    } catch (const std::exception &e) {
        DBG("link_per_client_elements failed: {}", e.what());

        if (q_sink)
            gst_object_unref(q_sink);
        if (aq_sink)
            gst_object_unref(aq_sink);
        FREE_PAD(ctx->tee_src_pad, self_->tee);
        FREE_PAD(ctx->audio_tee_src_pad, self_->atee);
    }
}

void WebRtcWebSocket::process_offer(std::shared_ptr<SessionContext> ctx, const json &jsn) {

    auto sdp = jsn["sdp"].get<std::string>();
    GstSDPMessage *sdp_message = nullptr;
    if (gst_sdp_message_new_from_text(sdp.c_str(), &sdp_message) != GST_SDP_OK) {
        DBG("Failed to parse SDP offer");
        return;
    }

    ctx->pt_video_vp8 = find_pt_for_codec(sdp_message, "video", "VP8");
    ctx->pt_audio_opus = find_pt_for_codec(sdp_message, "audio", "opus");

    set_video_pt(ctx.get());
    set_audio_pt(ctx.get());

    link_per_client_elements(ctx.get());

    gst_element_sync_state_with_parent(ctx->webrtcbin);

    DBG("Offer PTs: VP8={}, opus={}", ctx->pt_video_vp8, ctx->pt_audio_opus);

    auto offer = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp_message);
    auto promise = gst_promise_new_with_change_func(on_set_remote_description, ctx.get(), nullptr);

    g_signal_emit_by_name(ctx->webrtcbin, "set-remote-description", offer, promise);
    gst_webrtc_session_description_free(offer);

    DBG("Setting remote description");
}

void WebRtcWebSocket::process_canditate(std::shared_ptr<SessionContext> ctx, const json &jsn) {
    DBG("Received ICE candidate");

    auto ice = jsn["ice"];
    auto candidate = ice["candidate"].get<std::string>();
    auto sdpMLineIndex = static_cast<guint>(ice["sdpMLineIndex"].get<int>());

    g_signal_emit_by_name(ctx->webrtcbin, "add-ice-candidate", sdpMLineIndex, candidate.c_str());

    DBG("Added ICE candidate: candidate={} mlindex={}", candidate, sdpMLineIndex);
}

void WebRtcWebSocket::on_message(connection_hdl hdl, ws_server::message_ptr msg) {
    DBG("on_message");

    try {
        std::lock_guard<std::mutex> mutex_guard(webrtc_session_mutex);
        auto it = webrtc_sessions.find(hdl);
        if (it == webrtc_sessions.end()) {
            DBG("No session context for this connection");
            return;
        }
        auto ctx = it->second;

        const std::string payload = msg->get_payload();
        json jsn = json::parse(payload);

        auto type = jsn["type"].get<std::string>();

        if (type == "offer") {
            process_offer(ctx, jsn);
        } else if (type == "candidate") {
            process_canditate(ctx, jsn);
        }

    } catch (const std::exception &e) {
        DBG("on_message exception: {}", e.what());
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
    DBG("Negotiation needed");
}

static void send_text(SessionContext *ctx, const std::string &text) {
    try {
        ctx->ws->send(ctx->hdl, text, websocketpp::frame::opcode::text);
    } catch (const websocketpp::exception &e) {
        DBG("WebSocket send error: {}", e.what());
    }
}

static void
send_ice_candidate_message(SessionContext *ctx, guint mlineindex, const gchar *candidate) {
    DBG("Sending ICE candidate: mlineindex={}, candidate={}", mlineindex, candidate);

    json msg;
    msg["type"] = "candidate";
    msg["ice"] = {{"candidate", candidate}, {"sdpMLineIndex", mlineindex}};

    send_text(ctx, msg.dump());

    DBG("ICE candidate sent");
}

static void
on_ice_candidate(GstElement *webrtc, guint mlineindex, gchar *candidate, gpointer user_data) {
    DBG("on_ice_candidate");

    auto ctx = static_cast<SessionContext *>(user_data);

    if (!candidate || candidate[0] == '\0') {
        DBG("ICE end-of-candidates for mline {} (not sending)", mlineindex);
        send_ice_candidate_message(ctx, mlineindex, "");
        return;
    }

    DBG("ICE candidate generated: mlineindex={} candidate={}", mlineindex, candidate);

    send_ice_candidate_message(ctx, mlineindex, candidate);
}

static void on_answer_created(GstPromise *promise, gpointer user_data) {
    DBG("on_answer_created");

    auto *ctx = static_cast<SessionContext *>(user_data);

    GstWebRTCSessionDescription *answer = nullptr;
    const GstStructure *reply = gst_promise_get_reply(promise);
    if (!reply ||
        !gst_structure_get(
            reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, nullptr) ||
        !answer) {
        DBG("No answer in promise reply");
        gst_promise_unref(promise);
        return;
    }

    auto local_promise = gst_promise_new();
    g_signal_emit_by_name(ctx->webrtcbin, "set-local-description", answer, local_promise);
    gst_promise_unref(local_promise);

    json sdp_json;
    auto sdp_text = gst_sdp_message_as_text(answer->sdp);
    sdp_json["type"] = "answer";
    sdp_json["sdp"] = sdp_text ? sdp_text : "";
    send_text(ctx, sdp_json.dump());
    g_free(sdp_text);

    gst_webrtc_session_description_free(answer);
    gst_promise_unref(promise);
}

static void on_set_remote_description(GstPromise *promise, gpointer user_data) {
    DBG("on_set_remote_description");

    auto ctx = static_cast<SessionContext *>(user_data);

    auto answer_promise = gst_promise_new_with_change_func(on_answer_created, ctx, nullptr);

    g_signal_emit_by_name(ctx->webrtcbin, "create-answer", nullptr, answer_promise);

    gst_promise_unref(promise);
}
