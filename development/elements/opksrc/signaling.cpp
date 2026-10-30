/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "signaling.h"
#include "opksrc.h"
#include <system_error>

#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>

#define ASIO_STANDALONE
#include <asio.hpp>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtemplate-id-cdtor"
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#pragma GCC diagnostic pop

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

using Server = websocketpp::server<websocketpp::config::asio>;
using Handle = websocketpp::connection_hdl;
using Json = nlohmann::json;

constexpr gint64 kMediaTimeout = 15LL * G_USEC_PER_SEC;

struct PromiseResult {
    std::mutex mutex;
    std::condition_variable changed;
    bool completed = false;
};

using Promise = std::unique_ptr<GstPromise, decltype(&gst_promise_unref)>;

static Promise
invoke(GstElement *peer, const char *action, GstWebRTCSessionDescription *description) {
    auto result = std::make_shared<PromiseResult>();
    auto *data = new std::shared_ptr<PromiseResult>(result);
    Promise promise(gst_promise_new_with_change_func(
                        [](GstPromise *, gpointer user_data) {
                            auto state = *static_cast<std::shared_ptr<PromiseResult> *>(user_data);
                            std::scoped_lock guard(state->mutex);
                            state->completed = true;
                            state->changed.notify_all();
                        },
                        data,
                        [](gpointer user_data) {
                            delete static_cast<std::shared_ptr<PromiseResult> *>(user_data);
                        }),
                    gst_promise_unref);
    g_signal_emit_by_name(peer, action, description, promise.get());
    {
        std::unique_lock lock(result->mutex);
        if (!result->changed.wait_for(
                lock, std::chrono::seconds(5), [&] { return result->completed; })) {
            lock.unlock();
            gst_promise_interrupt(promise.get());
            throw std::runtime_error("WebRTC operation timed out");
        }
    }
    if (gst_promise_wait(promise.get()) != GST_PROMISE_RESULT_REPLIED) {
        throw std::runtime_error("WebRTC operation interrupted");
    }
    const auto *reply = gst_promise_get_reply(promise.get());
    if (reply && gst_structure_has_field(reply, "error")) {
        throw std::runtime_error("WebRTC rejected SDP");
    }
    return promise;
}

struct Session {
    std::atomic_bool active{true};
    std::atomic<gint64> last_audio{0};
    std::atomic<gint64> last_video{0};
    std::atomic_bool audio_enabled{false};
    std::atomic_bool video_enabled{false};
    guint media_count = 0;
    std::shared_ptr<Server> server;
    Handle handle;
    Server::timer_ptr watchdog;
    gint64 grace_until = g_get_monotonic_time() + kMediaTimeout;
    gulong ice_handler = 0;
    gulong audio_probe = 0;
    gulong video_probe = 0;
    bool offered = false;
};

static bool same_handle(const Handle &first, const Handle &second) {
    return !first.owner_before(second) && !second.owner_before(first);
}

static void send(const std::shared_ptr<Session> &session, const Json &message) {
    if (!session->active) {
        return;
    }
    websocketpp::lib::error_code error;
    session->server->send(session->handle, message.dump(), websocketpp::frame::opcode::text, error);
    if (error) {
        session->server->close(session->handle,
                               websocketpp::close::status::internal_endpoint_error,
                               "Signaling send failed",
                               error);
    }
}

static void on_ice(GstElement *, guint index, gchar *candidate, gpointer user_data) {
    auto session = static_cast<std::weak_ptr<Session> *>(user_data)->lock();
    if (!session || !session->active) {
        return;
    }
    send(session,
         {{"type", "candidate"},
          {"ice", {{"candidate", candidate ? candidate : ""}, {"sdpMLineIndex", index}}}});
}

static GstPadProbeReturn on_audio(GstPad *, GstPadProbeInfo *info, gpointer user_data) {
    const auto session = static_cast<std::weak_ptr<Session> *>(user_data)->lock();
    GstBuffer *const buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (session && session->active && session->audio_enabled && gst_buffer_get_size(buffer) > 0) {
        session->last_audio = g_get_monotonic_time();
    }
    return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn on_video(GstPad *, GstPadProbeInfo *info, gpointer user_data) {
    const auto session = static_cast<std::weak_ptr<Session> *>(user_data)->lock();
    if (session && session->active && session->video_enabled &&
        gst_buffer_get_size(GST_PAD_PROBE_INFO_BUFFER(info)) > 0) {
        session->last_video = g_get_monotonic_time();
    }
    return GST_PAD_PROBE_OK;
}

struct OfferedMedia {
    bool audio = false;
    bool video = false;
    guint count = 0;
};

static bool sending_media(const GstSDPMessage *const sdp, const GstSDPMedia *const media) {
    const char *direction = nullptr;
    for (const char *const candidate : {"sendonly", "sendrecv", "recvonly", "inactive"}) {
        if (gst_sdp_media_get_attribute_val(media, candidate))
            direction = candidate;
    }
    if (!direction) {
        for (const char *const candidate : {"sendonly", "sendrecv", "recvonly", "inactive"}) {
            if (gst_sdp_message_get_attribute_val(sdp, candidate))
                direction = candidate;
        }
    }
    if (direction && g_str_equal(direction, "recvonly")) {
        throw std::runtime_error("Expected a sending or inactive track");
    }
    return (!direction || !g_str_equal(direction, "inactive")) &&
           (gst_sdp_media_get_port(media) != 0 ||
            gst_sdp_media_get_attribute_val(media, "bundle-only"));
}

static GstCaps *codec_caps(const GstSDPMedia *const media) {
    const bool video = g_strcmp0(gst_sdp_media_get_media(media), "video") == 0;
    GstCaps *const supported = gst_caps_new_empty();
    for (guint index = 0; index < gst_sdp_media_formats_len(media); ++index) {
        const gchar *const format = gst_sdp_media_get_format(media, index);
        gchar *end = nullptr;
        const auto payload = g_ascii_strtoull(format, &end, 10);
        if (!end || *end || payload < 96 || payload > 127)
            continue;
        GstCaps *const caps = gst_sdp_media_get_caps_from_media(media, static_cast<gint>(payload));
        bool matches = false;
        if (caps && !gst_caps_is_empty(caps)) {
            const auto *const structure = gst_caps_get_structure(caps, 0);
            const gchar *const encoding = gst_structure_get_string(structure, "encoding-name");
            gint rate = 0;
            matches = encoding && g_ascii_strcasecmp(encoding, video ? "VP8" : "OPUS") == 0 &&
                      gst_structure_get_int(structure, "clock-rate", &rate) &&
                      rate == (video ? 90000 : 48000);
        }
        if (matches) {
            gst_structure_set_name(gst_caps_get_structure(caps, 0), "application/x-rtp");
            gst_caps_append(supported, caps);
        } else if (caps)
            gst_caps_unref(caps);
    }
    return supported;
}

static OfferedMedia validate_offer(const GstSDPMessage *const sdp) {
    OfferedMedia offered;
    offered.count = gst_sdp_message_medias_len(sdp);
    if (!offered.count)
        throw std::runtime_error("Invalid media count");
    for (guint index = 0; index < offered.count; ++index) {
        const auto *const media = gst_sdp_message_get_media(sdp, index);
        const bool audio = g_strcmp0(gst_sdp_media_get_media(media), "audio") == 0;
        const bool video = g_strcmp0(gst_sdp_media_get_media(media), "video") == 0;
        if (!audio && !video)
            throw std::runtime_error("Unsupported media type");
        if (!sending_media(sdp, media))
            continue;
        GstCaps *const caps = codec_caps(media);
        const bool supported = !gst_caps_is_empty(caps);
        gst_caps_unref(caps);
        if (!supported || (audio && offered.audio) || (video && offered.video)) {
            throw std::runtime_error("Expected at most one sending Opus and VP8 track");
        }
        if (audio)
            offered.audio = true;
        if (video)
            offered.video = true;
    }
    return offered;
}

struct OpkSrcSignaling::Impl {
    GstOpkSrc *source;
    std::shared_ptr<Server> server;
    std::thread worker;
    std::atomic_bool stopping{false};
    std::shared_ptr<Session> session{};
    std::set<Handle, std::owner_less<Handle>> connections;

    explicit Impl(GstOpkSrc *source) : source(source) {}

    void cleanup() {
        if (!session) {
            return;
        }
        session->active = false;
        session->watchdog->cancel();
        gst_pad_remove_probe(source->audio_pad, session->audio_probe);
        gst_pad_remove_probe(source->video_pad, session->video_probe);
        g_signal_handler_disconnect(source->webrtcbin, session->ice_handler);
        session.reset();
        if (!gst_opk_src_reset_webrtc(source)) {
            GST_ELEMENT_ERROR(source, CORE, FAILED, ("Unable to reset WebRTC receiver"), (nullptr));
        }
    }

    void close(const Handle &handle, const char *reason) {
        websocketpp::lib::error_code error;
        server->close(handle, websocketpp::close::status::policy_violation, reason, error);
    }

    void check_media() {
        g_rec_mutex_lock(&source->media_lock);
        const bool playing = source->media_state == GST_STATE_PLAYING;
        g_rec_mutex_unlock(&source->media_lock);

        const auto now = g_get_monotonic_time();
        const auto last_audio = session->last_audio.load();
        const auto last_video = session->last_video.load();
        const bool audio_live =
            session->audio_enabled && last_audio && now - last_audio < kMediaTimeout;
        const bool video_live =
            session->video_enabled && last_video && now - last_video < kMediaTimeout;
        g_object_set(source->selector,
                     "active-pad",
                     audio_live ? source->audio_pad : source->silence_pad,
                     nullptr);
        gst_opk_src_select_video(source, video_live);

        if (session->offered && !playing) {
            session->grace_until = now + kMediaTimeout;
        }
        const auto deadline =
            std::max(session->grace_until, std::max(last_audio, last_video) + kMediaTimeout);
        if (now >= deadline &&
            (!session->offered || session->audio_enabled || session->video_enabled)) {
            close(session->handle, "Capture media timed out");
            cleanup();
            return;
        }
        watch();
    }

    void watch() {
        const std::weak_ptr<Session> watched = session;
        session->watchdog = server->set_timer(100, [this, watched](const auto &error) {
            if (!error && !stopping && session && watched.lock() == session) {
                check_media();
            }
        });
    }

    void open(const Handle &handle) {
        connections.insert(handle);
        if (stopping || session) {
            close(handle, "Capture receiver is busy");
            return;
        }
        if (!source->webrtcbin) {
            close(handle, "WebRTC receiver unavailable");
            return;
        }
        session = std::make_shared<Session>();
        session->server = server;
        session->handle = handle;
        session->audio_probe = gst_pad_add_probe(
            source->audio_pad,
            GST_PAD_PROBE_TYPE_BUFFER,
            on_audio,
            new std::weak_ptr<Session>(session),
            [](gpointer data) { delete static_cast<std::weak_ptr<Session> *>(data); });
        session->video_probe = gst_pad_add_probe(
            source->video_pad,
            GST_PAD_PROBE_TYPE_BUFFER,
            on_video,
            new std::weak_ptr<Session>(session),
            [](gpointer data) { delete static_cast<std::weak_ptr<Session> *>(data); });
        session->ice_handler = g_signal_connect_data(
            source->webrtcbin,
            "on-ice-candidate",
            G_CALLBACK(on_ice),
            new std::weak_ptr<Session>(session),
            [](gpointer data, GClosure *) { delete static_cast<std::weak_ptr<Session> *>(data); },
            GConnectFlags(0));
        watch();
    }

    void offer(const Json &message) {
        const auto text = message.at("sdp").get<std::string>();
        if (text.empty() || text.find('\0') != std::string::npos) {
            throw std::runtime_error("Invalid SDP");
        }
        GstSDPMessage *sdp = nullptr;
        if (gst_sdp_message_new_from_text(text.c_str(), &sdp) != GST_SDP_OK) {
            throw std::runtime_error("Invalid SDP");
        }
        using Description = std::unique_ptr<GstWebRTCSessionDescription,
                                            decltype(&gst_webrtc_session_description_free)>;
        Description remote(gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp),
                           gst_webrtc_session_description_free);
        const auto offered = validate_offer(sdp);
        const bool newly_enabled = (!session->audio_enabled && offered.audio) ||
                                   (!session->video_enabled && offered.video);
        if (!offered.audio) {
            session->audio_enabled = false;
            session->last_audio = 0;
            g_object_set(source->selector, "active-pad", source->silence_pad, nullptr);
        }
        if (!offered.video) {
            session->video_enabled = false;
            session->last_video = 0;
            gst_opk_src_select_video(source, false);
        }
        invoke(source->webrtcbin, "set-remote-description", remote.get());
        for (guint index = 0; index < offered.count; ++index) {
            GstWebRTCRTPTransceiver *transceiver = nullptr;
            g_signal_emit_by_name(source->webrtcbin, "get-transceiver", index, &transceiver);
            if (!transceiver)
                throw std::runtime_error("Missing receive transceiver");
            const auto *const media = gst_sdp_message_get_media(sdp, index);
            GstCaps *const caps = codec_caps(media);
            g_object_set(transceiver,
                         "direction",
                         GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY,
                         "codec-preferences",
                         caps,
                         nullptr);
            gst_caps_unref(caps);
            gst_object_unref(transceiver);
        }
        auto promise = invoke(source->webrtcbin, "create-answer", nullptr);
        GstWebRTCSessionDescription *answer = nullptr;
        const auto *reply = gst_promise_get_reply(promise.get());
        if (!reply ||
            !gst_structure_get(
                reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, nullptr) ||
            !answer) {
            throw std::runtime_error("Unable to create answer");
        }
        Description local(answer, gst_webrtc_session_description_free);
        invoke(source->webrtcbin, "set-local-description", local.get());
        gchar *answer_text = gst_sdp_message_as_text(answer->sdp);
        const std::string answer_sdp = answer_text ? answer_text : "";
        g_free(answer_text);
        session->offered = true;
        session->media_count = offered.count;
        if (session->audio_enabled != offered.audio)
            session->last_audio = 0;
        if (session->video_enabled != offered.video)
            session->last_video = 0;
        session->audio_enabled = offered.audio;
        session->video_enabled = offered.video;
        if (!offered.audio)
            g_object_set(source->selector, "active-pad", source->silence_pad, nullptr);
        if (!offered.video)
            gst_opk_src_select_video(source, false);
        if (newly_enabled)
            session->grace_until = g_get_monotonic_time() + kMediaTimeout;
        send(session, {{"type", "answer"}, {"sdp", answer_sdp}});
    }

    void candidate(const Json &message) {
        const auto &ice = message.at("ice");
        const auto text = ice.at("candidate").get<std::string>();
        const auto &index = ice.at("sdpMLineIndex");
        if (!session->offered || !index.is_number_unsigned() ||
            index.get<uint64_t>() >= session->media_count ||
            text.find_first_of("\r\n") != std::string::npos ||
            text.find('\0') != std::string::npos) {
            throw std::runtime_error("Invalid ICE candidate");
        }
        g_signal_emit_by_name(
            source->webrtcbin, "add-ice-candidate", index.get<guint>(), text.c_str());
    }

    void message(const Handle &handle, const Server::message_ptr &incoming) {
        if (stopping || !session || !same_handle(session->handle, handle)) {
            return;
        }
        try {
            if (incoming->get_opcode() != websocketpp::frame::opcode::text) {
                throw std::runtime_error("Expected text signaling");
            }
            const auto parsed = Json::parse(incoming->get_payload());
            const auto type = parsed.at("type").get<std::string>();
            if (type == "offer") {
                offer(parsed);
            } else if (type == "candidate") {
                candidate(parsed);
            } else {
                throw std::runtime_error("Unknown signaling message");
            }
        } catch (const std::exception &error) {
            GST_WARNING_OBJECT(source, "WebRTC negotiation failed: %s", error.what());
            close(handle, "Invalid WebRTC negotiation");
            cleanup();
        }
    }

    bool start() {
        if (server) {
            return true;
        }
        stopping = false;
        try {
            server = std::make_shared<Server>();
            server->clear_access_channels(websocketpp::log::alevel::all);
            server->clear_error_channels(websocketpp::log::elevel::all);
            server->init_asio();
            server->set_reuse_addr(true);
            server->set_max_message_size(65536);
            server->set_open_handshake_timeout(2000);
            server->set_close_handshake_timeout(1000);
            server->set_open_handler([this](const Handle &handle) { open(handle); });
            server->set_close_handler([this](const Handle &handle) {
                connections.erase(handle);
                if (!stopping && session && same_handle(session->handle, handle)) {
                    cleanup();
                }
            });
            server->set_message_handler(
                [this](const Handle &handle, const Server::message_ptr &incoming) {
                    message(handle, incoming);
                });
            server->listen(source->ws_port);
            server->start_accept();
            worker = std::thread([this] { run_worker(); });
            return true;
        } catch (const std::exception &error) {
            GST_WARNING_OBJECT(source, "Cannot start WebRTC signaling: %s", error.what());
            stop();
            return false;
        }
    }

    void run_worker() {
        try {
            server->run();
        } catch (const std::exception &error) {
            GST_ELEMENT_ERROR(
                source, RESOURCE, FAILED, ("WebRTC signaling failed"), ("%s", error.what()));
        }
    }

    void stop() {
        try {
            stopping = true;
            if (server) {
                server->get_io_service().post([this] {
                    websocketpp::lib::error_code error;
                    if (session) {
                        session->watchdog->cancel();
                    }
                    server->stop_listening(error);
                    for (const auto &handle : connections) {
                        server->close(handle, websocketpp::close::status::going_away, "", error);
                    }
                });
                if (worker.joinable()) {
                    worker.join();
                } else {
                    server->run();
                }
                cleanup();
                connections.clear();
                server.reset();
            }
        } catch (std::system_error &e) {
            GST_ELEMENT_ERROR(
                source, RESOURCE, FAILED, ("Signaling could not stop clearly"), ("%s", e.what()));
        }
    }
};

OpkSrcSignaling::OpkSrcSignaling(GstOpkSrc *source) : impl_(std::make_unique<Impl>(source)) {}
OpkSrcSignaling::~OpkSrcSignaling() {
    stop();
}
bool OpkSrcSignaling::start() {
    return impl_->start();
}
void OpkSrcSignaling::stop() {
    impl_->stop();
}
