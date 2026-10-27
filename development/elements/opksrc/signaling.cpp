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

constexpr gint64 kMediaTimeout = 15 * G_USEC_PER_SEC;

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
    std::shared_ptr<Server> server;
    Handle handle;
    Server::timer_ptr watchdog;
    gint64 grace_until = g_get_monotonic_time() + kMediaTimeout;
    gulong ice_handler = 0;
    gulong audio_probe = 0;
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
    if (session && session->active && gst_buffer_get_size(buffer) > 0) {
        session->last_audio = g_get_monotonic_time();
    }
    return GST_PAD_PROBE_OK;
}

static bool supports_opus(const GstSDPMessage *sdp) {
    if (gst_sdp_message_medias_len(sdp) != 1) {
        return false;
    }
    const auto *media = gst_sdp_message_get_media(sdp, 0);
    if (g_strcmp0(gst_sdp_media_get_media(media), "audio") != 0 ||
        gst_sdp_media_get_port(media) == 0 || gst_sdp_media_get_attribute_val(media, "recvonly") ||
        gst_sdp_media_get_attribute_val(media, "inactive") ||
        gst_sdp_message_get_attribute_val(sdp, "recvonly") ||
        gst_sdp_message_get_attribute_val(sdp, "inactive")) {
        return false;
    }
    for (guint index = 0; index < gst_sdp_media_formats_len(media); ++index) {
        const gchar *format = gst_sdp_media_get_format(media, index);
        gchar *end = nullptr;
        const auto payload = g_ascii_strtoull(format, &end, 10);
        if (!end || *end || payload < 96 || payload > 127) {
            continue;
        }
        GstCaps *caps = gst_sdp_media_get_caps_from_media(media, static_cast<gint>(payload));
        bool opus = false;
        if (caps && !gst_caps_is_empty(caps)) {
            const auto *structure = gst_caps_get_structure(caps, 0);
            const gchar *encoding = gst_structure_get_string(structure, "encoding-name");
            gint rate = 0;
            opus = encoding && g_ascii_strcasecmp(encoding, "OPUS") == 0 &&
                   gst_structure_get_int(structure, "clock-rate", &rate) && rate == 48000;
        }
        if (caps) {
            gst_caps_unref(caps);
        }
        if (opus) {
            return true;
        }
    }
    return false;
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

    void check_audio() {
        g_rec_mutex_lock(&source->media_lock);
        const bool playing = source->media_state == GST_STATE_PLAYING;
        g_rec_mutex_unlock(&source->media_lock);

        const auto now = g_get_monotonic_time();
        const auto last_audio = session->last_audio.load();
        if (last_audio) {
            gboolean audio_active = FALSE;
            g_object_get(source->audio_pad, "active", &audio_active, nullptr);
            if (!audio_active) {
                g_object_set(source->selector, "active-pad", source->audio_pad, nullptr);
            }
        }

        if (session->offered && !playing) {
            session->grace_until = now + kMediaTimeout;
        }
        const auto deadline = std::max(session->grace_until, last_audio + kMediaTimeout);
        if (now >= deadline) {
            close(session->handle, "Microphone audio timed out");
            cleanup();
            return;
        }
        watch();
    }

    void watch() {
        const std::weak_ptr<Session> watched = session;
        session->watchdog = server->set_timer(100, [this, watched](const auto &error) {
            if (!error && !stopping && session && watched.lock() == session) {
                check_audio();
            }
        });
    }

    void open(const Handle &handle) {
        connections.insert(handle);
        if (stopping || session) {
            close(handle, "Microphone receiver is busy");
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
        if (session->offered) {
            throw std::runtime_error("Repeated offer");
        }
        const auto text = message.at("sdp").get<std::string>();
        if (text.empty() || text.find('\0') != std::string::npos) {
            throw std::runtime_error("Invalid SDP");
        }
        GstSDPMessage *sdp = nullptr;
        if (gst_sdp_message_new_from_text(text.c_str(), &sdp) != GST_SDP_OK) {
            throw std::runtime_error("Invalid SDP");
        }
        if (!supports_opus(sdp)) {
            gst_sdp_message_free(sdp);
            throw std::runtime_error("Expected one sending Opus audio track");
        }
        using Description = std::unique_ptr<GstWebRTCSessionDescription,
                                            decltype(&gst_webrtc_session_description_free)>;
        Description remote(gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp),
                           gst_webrtc_session_description_free);
        invoke(source->webrtcbin, "set-remote-description", remote.get());
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
        send(session, {{"type", "answer"}, {"sdp", answer_sdp}});
    }

    void candidate(const Json &message) {
        const auto &ice = message.at("ice");
        const auto text = ice.at("candidate").get<std::string>();
        const auto &index = ice.at("sdpMLineIndex");
        if (!session->offered || !index.is_number_unsigned() || index.get<uint64_t>() != 0 ||
            text.find_first_of("\r\n") != std::string::npos ||
            text.find('\0') != std::string::npos) {
            throw std::runtime_error("Invalid ICE candidate");
        }
        g_signal_emit_by_name(source->webrtcbin, "add-ice-candidate", 0u, text.c_str());
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
