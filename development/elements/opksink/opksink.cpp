/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

/* Build:
g++ -fPIC -shared -o libgstopksink.so opksink.cpp \
  $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-video-1.0 gstreamer-audio-1.0)
*/

// WebRTC in GST is unstable: this macro disables the warning
#include "glib-object.h"
#include "glib.h"
#include "gst/gstobject.h"
#include <gst/gstelement.h>

#define GST_USE_UNSTABLE_API

#include "Log.h"
#include "http_server.h"
#include "opksink.h"
#include "utils.h"
#include "webrtc_ws.h"

#include <websocketpp/common/connection_hdl.hpp>
#include <websocketpp/config/asio.hpp>
#include <websocketpp/frame.hpp>
#include <websocketpp/server.hpp>

#include <opk/Tools.h>

#include <httplib.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <dlfcn.h>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>

#ifndef PACKAGE
#define PACKAGE "opksink"
#endif

/* =============================== OpkSink ============================== */

static constexpr const char *OPK_SUPPORTED_RAW_VIDEO_CAPS =
    "video/x-raw, format={BGRA,RGB,I420,NV12,YUY2}";
static constexpr guint MAX_VP8_ENCODER_THREADS = 64;

static guint vp8_encoder_thread_count() {
    return std::clamp(g_get_num_processors() / 2, 1u, MAX_VP8_ENCODER_THREADS);
}

/* ===== Properties ===== */
enum {
    PROP_0,
    PROP_HOST,
    PROP_HTTP_PORT,
    PROP_WS_PORT,
    PROP_CTRL_PORT,
    PROP_STATIC_FILES,
    PROP_WEBRTC_STUN_SERVER,
    PROP_WEBRTC_TURN_SERVER,
    PROP_QOS_ENABLED,
};

static std::string env_or_empty(const char *name) {
    const char *value = std::getenv(name);
    return value ? value : "";
}

static std::string default_stun_server() {
    if (auto value = env_or_empty("OPK_WEBRTC_STUN_SERVER"); !value.empty()) {
        return value;
    }

    if (auto host = env_or_empty("WEBRTC_HOST_IP"); !host.empty()) {
        return "stun://" + host + ":3478";
    }

    return "stun://stun.l.google.com:19302";
}

static std::string default_turn_server() {
    if (auto value = env_or_empty("OPK_WEBRTC_TURN_SERVER"); !value.empty()) {
        return value;
    }

    const auto host = env_or_empty("WEBRTC_HOST_IP");
    if (host.empty()) {
        return "";
    }

    const auto username = env_or_empty("OPK_WEBRTC_TURN_USERNAME");
    const auto credential = env_or_empty("OPK_WEBRTC_TURN_CREDENTIAL");
    if (username.empty() || credential.empty()) {
        return "";
    }

    gchar *esc_user = g_uri_escape_string(username.c_str(), nullptr, TRUE);
    gchar *esc_cred = g_uri_escape_string(credential.c_str(), nullptr, TRUE);
    std::string url = "turn://" + std::string(esc_user ? esc_user : "") + ":" +
                      std::string(esc_cred ? esc_cred : "") + "@" + host + ":3478";
    g_free(esc_user);
    g_free(esc_cred);
    return url;
}

static gchar *default_static_files_location() {
    static constexpr char libraryAnchor = '\0';
    Dl_info libraryInfo{};
    if (dladdr(&libraryAnchor, &libraryInfo) == 0 || libraryInfo.dli_fname == nullptr) {
        return nullptr;
    }

    // Both flat development builds and release packages keep web/content two levels above the
    // plugin.
    const auto path = (std::filesystem::absolute(libraryInfo.dli_fname).parent_path() / ".." /
                       ".." / "web" / "content")
                          .lexically_normal();
    return g_strdup(path.c_str());
}

nlohmann::json PipelineStateReporter::report() const {
    nlohmann::json ret;

    if (self_) {
        // get the playing state
        GstState cur = GST_STATE_NULL;
        GstState pending = GST_STATE_NULL;
        gst_element_get_state(GST_ELEMENT(self_), &cur, &pending, 0);
        opk::log::debug("current state: {}, {}", int(cur), int(pending));

        const auto effective = pending == GST_STATE_VOID_PENDING ? cur : pending;
        ret["playing"] = effective == GST_STATE_PLAYING;

        // audio state
        ret["audio"] = has_audio_;
    }

    return ret;
}
GType gst_opk_sink_get_type(void);
#define GST_TYPE_OPK_SINK (gst_opk_sink_get_type())
#define GST_OPK_SINK(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_OPK_SINK, GstOpkSink))
G_DEFINE_TYPE(GstOpkSink, gst_opk_sink, GST_TYPE_BIN)

static void gst_opk_sink_stop_servers(GstOpkSink *self) {
    GstOpkPrivate *private_data = self->private_data;
    if (private_data->ctrl_websocket) {
        private_data->ctrl_websocket->stop();
    }
    if (private_data->http_server) {
        private_data->http_server->stop();
    }
    if (private_data->webrtc_websocket) {
        private_data->webrtc_websocket->stop();
    }
}

static bool gst_opk_sink_start_servers(GstOpkSink *self) {
    GstOpkPrivate *private_data = self->private_data;
    bool started = false;

    try {
        if (!private_data->webrtc_websocket) {
            private_data->webrtc_websocket = std::make_unique<WebRtcWebSocket>(self);
        }
        private_data->webrtc_websocket->start();

        if (!private_data->http_server) {
            private_data->http_server = std::make_unique<OpkSinkHttpServer>(self);
        }
        if (private_data->http_server->start() == OpkSinkHttpServerError::OK) {
            // Keep reporters attached to the same control object across restarts.
            if (!private_data->ctrl_websocket) {
                private_data->ctrl_websocket = std::make_unique<CtrlWebSocket>(self);
                private_data->ctrl_websocket->register_status_reporter(
                    "models", private_data->model_registry);
                private_data->ctrl_websocket->register_status_reporter(
                    "pipeline_state", private_data->pipeline_state_reporter);
            }
            private_data->ctrl_websocket->start();
            started = true;
        } else {
            GST_ELEMENT_ERROR(
                self,
                RESOURCE,
                OPEN_READ_WRITE,
                ("Unable to start HTTP server on port %d with static files from '%s'",
                 self->http_port,
                 self->static_files_location ? self->static_files_location : "(null)"),
                (nullptr));
        }
    } catch (const std::exception &error) {
        GST_ELEMENT_ERROR(
            self,
            RESOURCE,
            OPEN_READ_WRITE,
            ("Unable to start OpkSink servers (HTTP=%d, WebSocket=%d, control=%d): %s",
             self->http_port,
             self->ws_port,
             self->ctrl_port,
             error.what()),
            (nullptr));
    }

    if (!started) {
        gst_opk_sink_stop_servers(self);
    }

    return started;
}

static void gst_opk_sink_report_state_async(GstElement *element, gpointer user_data) {
    auto *self = reinterpret_cast<GstOpkSink *>(element);
    if (self->private_data && self->private_data->pipeline_state_reporter) {
        self->private_data->pipeline_state_reporter->set_paused();
    }
}

static GstStateChangeReturn gst_opk_sink_change_state(GstElement *element,
                                                      GstStateChange transition) {
    auto *self = GST_OPK_SINK(element);
    if (!self->private_data || !self->private_data->initialized) {
        GST_ERROR_OBJECT(self, "Cannot change state because OpkSink initialization failed");
        return GST_STATE_CHANGE_FAILURE;
    }

    // gst_parse_launch() applies element properties after instance initialization.
    if (transition == GST_STATE_CHANGE_NULL_TO_READY && !gst_opk_sink_start_servers(self)) {
        return GST_STATE_CHANGE_FAILURE;
    }
    if (transition == GST_STATE_CHANGE_READY_TO_NULL) {
        gst_opk_sink_stop_servers(self);
    }

    const auto result =
        GST_ELEMENT_CLASS(gst_opk_sink_parent_class)->change_state(element, transition);

    if (transition == GST_STATE_CHANGE_NULL_TO_READY && result == GST_STATE_CHANGE_FAILURE) {
        gst_opk_sink_stop_servers(self);
    }

    if (result != GST_STATE_CHANGE_FAILURE && self->private_data &&
        self->private_data->pipeline_state_reporter) {
        gst_element_call_async(element, gst_opk_sink_report_state_async, nullptr, nullptr);
    }

    return result;
}

static void
gst_opk_sink_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    auto *self = reinterpret_cast<GstOpkSink *>(object);
    switch (prop_id) {
    case PROP_HOST:
        g_free(self->host);
        self->host = g_value_dup_string(value);
        break;
    case PROP_HTTP_PORT:
        self->http_port = g_value_get_int(value);
        break;
    case PROP_WS_PORT:
        self->ws_port = g_value_get_int(value);
        break;
    case PROP_CTRL_PORT:
        self->ctrl_port = g_value_get_int(value);
        break;
    case PROP_STATIC_FILES:
        g_free(self->static_files_location);
        self->static_files_location = g_value_dup_string(value);
        break;
    case PROP_WEBRTC_STUN_SERVER:
        g_free(self->webrtc_stun_server);
        self->webrtc_stun_server = g_value_dup_string(value);
        break;
    case PROP_WEBRTC_TURN_SERVER:
        g_free(self->webrtc_turn_server);
        self->webrtc_turn_server = g_value_dup_string(value);
        break;
    case PROP_QOS_ENABLED:
        self->qos_enabled = g_value_get_boolean(value);
        if (self->drain_fakesink)
            g_object_set(
                self->drain_fakesink, "sync", self->qos_enabled, "qos", self->qos_enabled, nullptr);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        return;
    }
}

static void
gst_opk_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    auto *self = reinterpret_cast<GstOpkSink *>(object);
    switch (prop_id) {
    case PROP_HOST:
        g_value_set_string(value, self->host);
        break;
    case PROP_HTTP_PORT:
        g_value_set_int(value, self->http_port);
        break;
    case PROP_WS_PORT:
        g_value_set_int(value, self->ws_port);
        break;
    case PROP_CTRL_PORT:
        g_value_set_int(value, self->ctrl_port);
        break;
    case PROP_STATIC_FILES:
        g_value_set_string(value, self->static_files_location);
        break;
    case PROP_WEBRTC_STUN_SERVER:
        g_value_set_string(value, self->webrtc_stun_server);
        break;
    case PROP_WEBRTC_TURN_SERVER:
        g_value_set_string(value, self->webrtc_turn_server);
        break;
    case PROP_QOS_ENABLED:
        g_value_set_boolean(value, self->qos_enabled);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

/* ===== Pad templates ===== */
static GstStaticPadTemplate v_sink_template = GST_STATIC_PAD_TEMPLATE(
    "videosink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(OPK_SUPPORTED_RAW_VIDEO_CAPS));

static GstStaticPadTemplate a_sink_template = GST_STATIC_PAD_TEMPLATE(
    "audiosink", GST_PAD_SINK, GST_PAD_REQUEST, GST_STATIC_CAPS("audio/x-raw"));

/* ===== Request/Release pads (audio, MP2 path + audiorate) ===== */

static GstPad *gst_opk_sink_request_new_pad(GstElement *element,
                                            GstPadTemplate *templ,
                                            const gchar *name,
                                            const GstCaps *caps) {
    auto *self = reinterpret_cast<GstOpkSink *>(element);
    const gchar *templ_name = GST_PAD_TEMPLATE_NAME_TEMPLATE(templ);
    if (!self->private_data || !self->private_data->initialized) {
        GST_ERROR_OBJECT(self, "Cannot request a pad because OpkSink initialization failed");
        return nullptr;
    }

    if (g_strcmp0(templ_name, "audiosink") != 0) {
        return nullptr;
    }

    if (self->private_data && self->private_data->pipeline_state_reporter) {
        self->private_data->pipeline_state_reporter->set_audio(true);
    }

    // If already created, just return existing pad
    // This is probably not the best approach:
    //      one request -> one new pad
    //      what happens if somebody requests new pad,
    //      and we return an already-connected-pad
    if (self->audio_ghost_pad) {
        return nullptr;
    }

    // IMPORTANT: ghost an UNLINKED internal pad (the queue sink), not selector/request pads,
    // and not aconv sink if it's already linked in the internal chain.
    if (!self->ain_queue) {
        GST_ERROR_OBJECT(self, "audio_in_queue is NULL (init_audio not run?)");
        return nullptr;
    }

    GstPad *qin_sink = gst_element_get_static_pad(self->ain_queue, "sink");
    if (!qin_sink) {
        GST_ERROR_OBJECT(self, "Failed to get audio_in_queue:sink");
        return nullptr;
    }

    // Create REQUEST ghost pad
    self->audio_ghost_pad = gst_ghost_pad_new("audiosink", qin_sink);
    gst_object_unref(qin_sink);

    if (!self->audio_ghost_pad) {
        GST_ERROR_OBJECT(self, "Failed to create audio ghost pad");
        return nullptr;
    }

    gst_pad_set_active(self->audio_ghost_pad, TRUE);

    if (!gst_element_add_pad(GST_ELEMENT(self), self->audio_ghost_pad)) {
        GST_ERROR_OBJECT(self, "Failed to add audio ghost pad to element");
        gst_object_unref(self->audio_ghost_pad);
        self->audio_ghost_pad = nullptr;
        return nullptr;
    }

    // switch selector immediately to the "real" pad.
    // this can be done in pad probe. it would be better
    if (self->aselector && self->aselector_real_pad) {
        g_object_set(self->aselector, "active-pad", self->aselector_real_pad, nullptr);
        GST_INFO_OBJECT(self, "Audio request pad created, selector switched to real audio input");
    } else {
        GST_WARNING_OBJECT(self, "Selector or real pad not ready; cannot switch to real input");
    }

    return self->audio_ghost_pad;
}

static void gst_opk_sink_release_pad(GstElement *element, GstPad *pad) {
    auto *self = reinterpret_cast<GstOpkSink *>(element);
    gst_element_remove_pad(element, pad);
    if (pad == self->audio_ghost_pad) {
        self->audio_ghost_pad = nullptr;
    }
}

static void gst_opk_sink_stop_pipeline_async(GstElement *element, gpointer user_data) {
    auto *self = reinterpret_cast<GstOpkSink *>(element);
    GstElement *top = get_top_pipeline(element);

    // GST_IS_ELEMENT() is a macro performing a type check with no side effects
    if (top && GST_IS_ELEMENT(top)) { // NOSONAR
        GST_INFO_OBJECT(self, "EOS received, posting EOS message to top pipeline");
        gst_element_post_message(top, gst_message_new_eos(GST_OBJECT(top)));
        gst_object_unref(top);
        return;
    }
}

/* ===== Event handling ===== */
static gboolean gst_opk_sink_sink_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    auto *self = reinterpret_cast<GstOpkSink *>(parent);

    if (!self->private_data || !self->private_data->initialized) {
        gst_event_unref(event);
        return FALSE;
    }

    if (GST_EVENT_TYPE(event) == GST_EVENT_CUSTOM_DOWNSTREAM) {
        const GstStructure *structure = gst_event_get_structure(event);

        if (gst_structure_has_name(structure, "opk-model-register")) {
            if (auto status = model_status_from_registration(structure))
                self->private_data->model_registry->add_model(*status);

            // Consume the event (don't pass it further)
            gst_event_unref(event);
            return TRUE;
        } else if (gst_structure_has_name(structure, "opk-model-unregister")) {
            const gchar *element_name = gst_structure_get_string(structure, "element-name");

            if (element_name) {
                self->private_data->model_registry->del_model(element_name);
            }

            // Consume the event (don't pass it further)

            gst_event_unref(event);
            return TRUE;
        }
    }

    if (GST_EVENT_TYPE(event) == GST_EVENT_EOS) {
        GST_INFO_OBJECT(self, "End of stream received at sink pad event");
        gst_element_call_async(
            GST_ELEMENT(self), gst_opk_sink_stop_pipeline_async, nullptr, nullptr);
        // Forward EOS to the target pad to notify downstream elements of stream end
    }

    // Pass all events (including EOS) to the target pad
    GstPad *target = gst_ghost_pad_get_target(GST_GHOST_PAD(pad));
    if (target) {
        gboolean ret = gst_pad_send_event(target, event);
        gst_object_unref(target);
        return ret;
    }

    gst_event_unref(event);
    return FALSE;
}

/* ===== Lifecycle ===== */
static void gst_opk_sink_dispose(GObject *object) {
    if (!object) {
        return;
    }
    auto *self = reinterpret_cast<GstOpkSink *>(object);

    if (self->private_data) {
        gst_opk_sink_stop_servers(self);
        self->private_data->pipeline_state_reporter.reset();
    }

    // IMPORTANT: selector may be holding a ref via active-pad
    if (self->aselector) {
        g_object_set(self->aselector, "active-pad", NULL, nullptr);
    }

    release_request_pad_and_unref(self->aselector, &self->aselector_silence_pad);
    release_request_pad_and_unref(self->aselector, &self->aselector_real_pad);

    release_request_pad_and_unref(self->tee, &self->drain_tee_src_pad);
    release_request_pad_and_unref(self->atee, &self->audio_drain_tee_src_pad);

    G_OBJECT_CLASS(gst_opk_sink_parent_class)->dispose(object);
}

static void gst_opk_sink_finalize(GObject *object) {
    auto *self = reinterpret_cast<GstOpkSink *>(object);

    // Free properties
    g_clear_pointer(&self->host, g_free);
    g_clear_pointer(&self->static_files_location, g_free);
    g_clear_pointer(&self->webrtc_stun_server, g_free);
    g_clear_pointer(&self->webrtc_turn_server, g_free);

    // Free private data
    delete self->private_data;
    self->private_data = nullptr;

    G_OBJECT_CLASS(gst_opk_sink_parent_class)->finalize(object);
}

static GstElement *make_opksink_element(const gchar *factory_name, const gchar *element_name) {
#ifdef OPK_ENABLE_FACTORY_FAILURE_INJECTION
    const gchar *failed_element = g_getenv("OPK_TEST_OPKSINK_FAIL_ELEMENT");
    if (failed_element && g_strcmp0(failed_element, element_name) == 0) {
        GST_WARNING("Injecting OpkSink factory failure for %s", element_name);
        return nullptr;
    }
#endif

    return gst_element_factory_make(factory_name, element_name);
}

static bool should_fail_opksink_operation(const gchar *operation_name,
                                          const gchar *target_name = nullptr) {
#ifdef OPK_ENABLE_FACTORY_FAILURE_INJECTION
    const gchar *failed_operation = g_getenv("OPK_TEST_OPKSINK_FAIL_OPERATION");
    if (!failed_operation) {
        return false;
    }

    std::string qualified_name = operation_name;
    if (target_name) {
        qualified_name += "_";
        qualified_name += target_name;
    }

    if (qualified_name == failed_operation) {
        GST_WARNING("Injecting OpkSink operation failure for %s", qualified_name.c_str());
        return true;
    }
#else
    (void)operation_name;
    (void)target_name;
#endif
    return false;
}

static bool create_and_add_opksink_element(GstOpkSink *self,
                                           GstElement **member,
                                           const gchar *factory_name,
                                           const gchar *element_name) {
    GstElement *element = make_opksink_element(factory_name, element_name);
    if (!element) {
        GST_ERROR_OBJECT(self, "Failed to create element %s", element_name);
        return false;
    }

    if (should_fail_opksink_operation("add", element_name) ||
        !gst_bin_add(GST_BIN(self), element)) {
        GST_ERROR_OBJECT(self, "Failed to add %s to OpkSink", element_name);
        gst_object_unref(element);
        return false;
    }

    *member = element;
    return true;
}

static bool init_video(GstOpkSink *self) {
    auto fail = [self](const gchar *message) {
        GST_ERROR_OBJECT(self, "%s", message);
        return false;
    };

    if (!create_and_add_opksink_element(self, &self->vconv, "videoconvert", "vconv") ||
        !create_and_add_opksink_element(self, &self->queue, "queue", "vqueue") ||
        !create_and_add_opksink_element(self, &self->vp8enc, "vp8enc", "vp8enc") ||
        !create_and_add_opksink_element(self, &self->vclock, "identity", "vclock") ||
        !create_and_add_opksink_element(self, &self->tee, "tee", "rtp_tee")) {
        return false;
    }

    g_object_set(self->vclock, "sync", TRUE, nullptr);
    g_object_set(self->vp8enc, "deadline", 1, nullptr); // the frame shall be rendered realtime
    g_object_set(self->vp8enc, "target-bitrate", 0, nullptr);
    g_object_set(self->vp8enc, "cpu-used", 4, nullptr);
    g_object_set(self->vp8enc, "keyframe-max-dist", 60, nullptr); // max frames between key frames
    g_object_set(self->vp8enc, "threads", static_cast<gint>(vp8_encoder_thread_count()), nullptr);
    g_object_set(self->vp8enc, "error-resilient", 1, nullptr);

    if (should_fail_opksink_operation("link_video_chain") ||
        !gst_element_link_many(
            self->vconv, self->queue, self->vp8enc, self->vclock, self->tee, nullptr)) {
        GST_ERROR_OBJECT(self, "Failed to link video chain");
        return false;
    }

    if (!create_and_add_opksink_element(self, &self->drain_queue, "queue", "drain_queue") ||
        !create_and_add_opksink_element(
            self, &self->drain_fakesink, "fakesink", "drain_fakesink")) {
        return false;
    }

    g_object_set(self->drain_fakesink,
                 "sync",
                 self->qos_enabled,
                 "async",
                 FALSE,
                 "qos",
                 self->qos_enabled,
                 nullptr);

    if (should_fail_opksink_operation("link_video_drain") ||
        !gst_element_link(self->drain_queue, self->drain_fakesink)) {
        GST_ERROR_OBJECT(self, "Failed to link drain_queue -> drain_fakesink");
        return false;
    }

    if (!should_fail_opksink_operation("request_pad_video_drain")) {
        self->drain_tee_src_pad = gst_element_request_pad_simple(self->tee, "src_%u");
    }
    if (!self->drain_tee_src_pad) {
        return fail("Failed to request video drain pad from rtp_tee");
    }

    GstPad *drain_sink = should_fail_opksink_operation("get_pad_video_drain_sink")
                             ? nullptr
                             : gst_element_get_static_pad(self->drain_queue, "sink");
    if (!drain_sink) {
        return fail("Failed to get drain_queue sink pad");
    }

    const GstPadLinkReturn drain_link = should_fail_opksink_operation("pad_link_video_drain")
                                            ? GST_PAD_LINK_REFUSED
                                            : gst_pad_link(self->drain_tee_src_pad, drain_sink);
    gst_object_unref(drain_sink);
    if (drain_link != GST_PAD_LINK_OK) {
        GST_ERROR_OBJECT(self, "Failed to link rtp_tee to drain_queue: %d", drain_link);
        return false;
    }

    gst_element_sync_state_with_parent(self->drain_queue);
    gst_element_sync_state_with_parent(self->drain_fakesink);

    GstPad *video_sink = should_fail_opksink_operation("get_pad_video_sink")
                             ? nullptr
                             : gst_element_get_static_pad(self->vconv, "sink");
    if (!video_sink) {
        return fail("Failed to get vconv sink pad");
    }

    GstPad *video_ghost_pad = should_fail_opksink_operation("create_video_ghost_pad")
                                  ? nullptr
                                  : gst_ghost_pad_new("sink", video_sink);
    gst_object_unref(video_sink);
    if (!video_ghost_pad) {
        return fail("Failed to create video ghost pad");
    }

    gst_pad_set_event_function(video_ghost_pad, gst_opk_sink_sink_event);
    if (should_fail_opksink_operation("add_video_ghost_pad") ||
        !gst_element_add_pad(GST_ELEMENT(self), video_ghost_pad)) {
        GST_ERROR_OBJECT(self, "Failed to add video ghost pad to OpkSink");
        gst_object_unref(video_ghost_pad);
        return false;
    }

    return true;
}

static bool init_audio(GstOpkSink *self) {
    auto fail = [self](const gchar *message) {
        GST_ERROR_OBJECT(self, "%s", message);
        return false;
    };

    if (!create_and_add_opksink_element(
            self, &self->asilence_src, "audiotestsrc", "audio_silence_src") ||
        !create_and_add_opksink_element(self, &self->ain_queue, "queue", "audio_in_queue") ||
        !create_and_add_opksink_element(
            self, &self->aselector, "input-selector", "audio_selector") ||
        !create_and_add_opksink_element(self, &self->acapsfilter, "capsfilter", "audio_caps") ||
        !create_and_add_opksink_element(self, &self->aconv, "audioconvert", "aconv") ||
        !create_and_add_opksink_element(self, &self->aresample, "audioresample", "aresample") ||
        !create_and_add_opksink_element(self, &self->opusenc, "opusenc", "opusenc") ||
        !create_and_add_opksink_element(self, &self->aclock, "identity", "aclock") ||
        !create_and_add_opksink_element(self, &self->atee, "tee", "audio_tee")) {
        return false;
    }

    g_object_set(self->asilence_src, "wave", 4 /* silence */, "is-live", TRUE, nullptr);

    GstCaps *audio_caps = gst_caps_new_simple("audio/x-raw",
                                              "format",
                                              G_TYPE_STRING,
                                              "S16LE",
                                              "rate",
                                              G_TYPE_INT,
                                              48000,
                                              "channels",
                                              G_TYPE_INT,
                                              2,
                                              nullptr);
    g_object_set(self->acapsfilter, "caps", audio_caps, nullptr);
    gst_caps_unref(audio_caps);
    g_object_set(self->aclock, "sync", TRUE, nullptr);

    GstPad *silence_src_pad = should_fail_opksink_operation("get_pad_audio_silence_src")
                                  ? nullptr
                                  : gst_element_get_static_pad(self->asilence_src, "src");
    if (!silence_src_pad) {
        return fail("Failed to get audio_silence_src source pad");
    }

    if (!should_fail_opksink_operation("request_pad_audio_silence")) {
        self->aselector_silence_pad = gst_element_request_pad_simple(self->aselector, "sink_%u");
    }
    if (!self->aselector_silence_pad) {
        gst_object_unref(silence_src_pad);
        return fail("Failed to request silence pad from audio_selector");
    }

    GstPadLinkReturn link_result = should_fail_opksink_operation("pad_link_audio_silence")
                                       ? GST_PAD_LINK_REFUSED
                                       : gst_pad_link(silence_src_pad, self->aselector_silence_pad);
    gst_object_unref(silence_src_pad);
    if (link_result != GST_PAD_LINK_OK) {
        GST_ERROR_OBJECT(self, "Failed to link silence into audio_selector: %d", link_result);
        return false;
    }

    GstPad *real_src_pad = should_fail_opksink_operation("get_pad_audio_real_src")
                               ? nullptr
                               : gst_element_get_static_pad(self->ain_queue, "src");
    if (!real_src_pad) {
        return fail("Failed to get audio_in_queue source pad");
    }

    if (!should_fail_opksink_operation("request_pad_audio_real")) {
        self->aselector_real_pad = gst_element_request_pad_simple(self->aselector, "sink_%u");
    }
    if (!self->aselector_real_pad) {
        gst_object_unref(real_src_pad);
        return fail("Failed to request real-input pad from audio_selector");
    }

    link_result = should_fail_opksink_operation("pad_link_audio_real")
                      ? GST_PAD_LINK_REFUSED
                      : gst_pad_link(real_src_pad, self->aselector_real_pad);
    gst_object_unref(real_src_pad);
    if (link_result != GST_PAD_LINK_OK) {
        GST_ERROR_OBJECT(
            self, "Failed to link audio_in_queue into audio_selector: %d", link_result);
        return false;
    }

    if (should_fail_opksink_operation("link_audio_chain") ||
        !gst_element_link_many(self->aselector,
                               self->aconv,
                               self->aresample,
                               self->acapsfilter,
                               self->opusenc,
                               self->aclock,
                               self->atee,
                               nullptr)) {
        return fail("Failed to link audio_selector to audio_tee chain");
    }

    if (!create_and_add_opksink_element(
            self, &self->audio_drain_queue, "queue", "audio_drain_queue") ||
        !create_and_add_opksink_element(
            self, &self->audio_drain_fakesink, "fakesink", "audio_drain_fakesink")) {
        return false;
    }
    g_object_set(self->audio_drain_fakesink, "sync", FALSE, "async", FALSE, nullptr);

    if (should_fail_opksink_operation("link_audio_drain") ||
        !gst_element_link(self->audio_drain_queue, self->audio_drain_fakesink)) {
        return fail("Failed to link audio drain queue to fakesink");
    }

    if (!should_fail_opksink_operation("request_pad_audio_drain")) {
        self->audio_drain_tee_src_pad = gst_element_request_pad_simple(self->atee, "src_%u");
    }
    if (!self->audio_drain_tee_src_pad) {
        return fail("Failed to request drain pad from audio_tee");
    }

    GstPad *drain_sink_pad = should_fail_opksink_operation("get_pad_audio_drain_sink")
                                 ? nullptr
                                 : gst_element_get_static_pad(self->audio_drain_queue, "sink");
    if (!drain_sink_pad) {
        return fail("Failed to get audio_drain_queue sink pad");
    }

    link_result = should_fail_opksink_operation("pad_link_audio_drain")
                      ? GST_PAD_LINK_REFUSED
                      : gst_pad_link(self->audio_drain_tee_src_pad, drain_sink_pad);
    gst_object_unref(drain_sink_pad);
    if (link_result != GST_PAD_LINK_OK) {
        GST_ERROR_OBJECT(self, "Failed to link audio_tee to drain queue: %d", link_result);
        return false;
    }

    gst_element_sync_state_with_parent(self->asilence_src);
    gst_element_sync_state_with_parent(self->ain_queue);
    gst_element_sync_state_with_parent(self->aselector);
    gst_element_sync_state_with_parent(self->aconv);
    gst_element_sync_state_with_parent(self->aresample);
    gst_element_sync_state_with_parent(self->acapsfilter);
    gst_element_sync_state_with_parent(self->opusenc);
    gst_element_sync_state_with_parent(self->atee);
    gst_element_sync_state_with_parent(self->audio_drain_queue);
    gst_element_sync_state_with_parent(self->audio_drain_fakesink);

    return true;
}

static void gst_opk_sink_init(GstOpkSink *self) {
    self->private_data = new GstOpkPrivate();

    /* defaults */
    self->host = g_strdup("0.0.0.0");
    self->static_files_location = default_static_files_location();
    self->webrtc_stun_server = g_strdup(default_stun_server().c_str());
    self->webrtc_turn_server = g_strdup(default_turn_server().c_str());
    self->http_port = 9999;
    self->ws_port = 8000;
    self->ctrl_port = 8001;
    self->qos_enabled = false;

    if (!init_video(self) || !init_audio(self)) {
        GST_ERROR_OBJECT(self, "OpkSink initialization failed");
        return;
    }

    // private data
    self->private_data->model_registry = std::make_shared<ModelRegistry>();
    self->private_data->pipeline_state_reporter = std::make_shared<PipelineStateReporter>(self);

    self->private_data->initialized = true;
}

static void gst_opk_sink_class_init(GstOpkSinkClass *klass) {
    auto *gobject_class = G_OBJECT_CLASS(klass);
    auto *element_class = GST_ELEMENT_CLASS(klass);

    gobject_class->set_property = gst_opk_sink_set_property;
    gobject_class->get_property = gst_opk_sink_get_property;
    gobject_class->dispose = gst_opk_sink_dispose;
    gobject_class->finalize = gst_opk_sink_finalize;

    /* C++ flags helper */
    constexpr GParamFlags kRW =
        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /* properties */
    g_object_class_install_property(
        gobject_class,
        PROP_HOST,
        g_param_spec_string(
            "host", "Host", "The interface to bind the HTTP Server", "0.0.0.0", kRW));
    g_object_class_install_property(
        gobject_class,
        PROP_HTTP_PORT,
        g_param_spec_int("http-port", "HTTP Port", "HTTP Server Port", 1, 65535, 9999, kRW));
    g_object_class_install_property(
        gobject_class,
        PROP_WS_PORT,
        g_param_spec_int("ws-port", "WebSocket Port", "WebSocket Port", 1, 65535, 8000, kRW));
    g_object_class_install_property(
        gobject_class,
        PROP_CTRL_PORT,
        g_param_spec_int("ctrl-port", "Control Port", "Control Port", 1, 65535, 8001, kRW));
    g_object_class_install_property(
        gobject_class,
        PROP_STATIC_FILES,
        g_param_spec_string("static-files",
                            "Static Files Location",
                            "Location of the static files for HTTP Server",
                            nullptr,
                            kRW));
    g_object_class_install_property(
        gobject_class,
        PROP_WEBRTC_STUN_SERVER,
        g_param_spec_string("webrtc-stun-server",
                            "WebRTC STUN Server",
                            "STUN server URL passed to webrtcbin, for example stun://host:3478",
                            nullptr,
                            kRW));
    g_object_class_install_property(gobject_class,
                                    PROP_WEBRTC_TURN_SERVER,
                                    g_param_spec_string("webrtc-turn-server",
                                                        "WebRTC TURN Server",
                                                        "TURN server URL advertised to browsers",
                                                        nullptr,
                                                        kRW));
    g_object_class_install_property(
        gobject_class,
        PROP_QOS_ENABLED,
        g_param_spec_boolean("qos-enabled",
                             "QoS enabled",
                             "Enable experimental QoS feedback from the video drain",
                             false,
                             kRW));

    /* pads */
    gst_element_class_add_static_pad_template(element_class, &v_sink_template);
    gst_element_class_add_static_pad_template(element_class, &a_sink_template);

    /* request/release handlers for audio */
    element_class->request_new_pad = gst_opk_sink_request_new_pad;
    element_class->release_pad = gst_opk_sink_release_pad;
    element_class->change_state = gst_opk_sink_change_state;

    gst_element_class_set_static_metadata(
        element_class,
        "OpkSink (video+audio → raw video+audio -> VP8 -> WebRTC)",
        "Sink/Network/Bin",
        "Encodes & muxes raw video+audio and sends them to WebRTC",
        "Arm Limited <perception-fdbck@arm.com>");
}

/* ===== Plugin boilerplate ===== */
static gboolean opksink_plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "opksink", GST_RANK_NONE, GST_TYPE_OPK_SINK);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  opksink,
                  "OpkSink bin: raw video+audio -> VP8 -> WebRTC ",
                  opksink_plugin_init,
                  "1.0",
                  "Apache 2.0",
                  PACKAGE,
                  "https://example.com")
