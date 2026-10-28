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

#include "opksrc.h"
#include "signaling.h"

#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>

#ifndef PACKAGE
#define PACKAGE "opksrc"
#endif

GType gst_opk_src_get_type();
#define GST_TYPE_OPK_SRC (gst_opk_src_get_type())

G_DEFINE_TYPE(GstOpkSrc, gst_opk_src, GST_TYPE_BIN)

enum {
    PROP_0,
    PROP_WS_PORT,
    PROP_RATE,
};

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(
        "audio/x-raw, format=F32LE, layout=interleaved, rate=[1,2147483647], channels=1"));

static void update_output_caps(GstOpkSrc *self) {
    if (!self->output_filter) {
        return;
    }
    GstCaps *caps = gst_static_pad_template_get_caps(&src_template);
    caps = gst_caps_make_writable(caps);
    gst_caps_set_simple(caps, "rate", G_TYPE_INT, self->rate, nullptr);
    g_object_set(self->output_filter, "caps", caps, nullptr);
    gst_caps_unref(caps);
}

static void gst_opk_src_pad_added(GstElement *webrtcbin, GstPad *pad, gpointer user_data) {
    auto *self = static_cast<GstOpkSrc *>(user_data);
    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC) {
        return;
    }

    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps) {
        caps = gst_pad_query_caps(pad, nullptr);
    }
    GstCaps *opus_caps = gst_caps_from_string(
        "application/x-rtp, media=audio, encoding-name=OPUS, clock-rate=48000");
    const bool is_opus = caps && !gst_caps_is_any(caps) && gst_caps_is_subset(caps, opus_caps) &&
                         !gst_caps_is_empty(caps);
    gst_caps_unref(opus_caps);
    if (caps) {
        gst_caps_unref(caps);
    }
    if (!is_opus) {
        GST_WARNING_OBJECT(self, "Ignoring non-Opus WebRTC pad %s", GST_PAD_NAME(pad));
        return;
    }

    GstPad *sink_pad = gst_element_get_static_pad(self->depay, "sink");
    if (gst_pad_is_linked(sink_pad)) {
        GST_WARNING_OBJECT(self, "Only one incoming audio track is supported");
    } else {
        const auto result = gst_pad_link(pad, sink_pad);
        if (result != GST_PAD_LINK_OK) {
            GST_ELEMENT_ERROR(
                self, CORE, PAD, ("Failed to link WebRTC audio"), ("Pad link result: %d", result));
        }
    }
    gst_object_unref(sink_pad);
}

static GstElement *create_and_add_opksrc_element(GstOpkSrc *self, const gchar *factory_name) {
    GstElement *element = gst_element_factory_make(factory_name, factory_name);
    if (!element) {
        GST_ERROR_OBJECT(self, "Failed to create %s", factory_name);
        return nullptr;
    }
    if (!gst_bin_add(GST_BIN(self), element)) {
        GST_ERROR_OBJECT(self, "Failed to add %s", factory_name);
        gst_object_unref(element);
        return nullptr;
    }
    return element;
}

static bool link_selector_input(GstOpkSrc *self, GstElement *input, GstPad **selector_pad) {
    *selector_pad = gst_element_request_pad_simple(self->selector, "sink_%u");
    if (!*selector_pad) {
        GST_ERROR_OBJECT(self, "Failed to request audio selector pad");
        return false;
    }
    GstPad *source_pad = gst_element_get_static_pad(input, "src");
    const auto result = gst_pad_link(source_pad, *selector_pad);
    gst_object_unref(source_pad);
    if (result != GST_PAD_LINK_OK) {
        GST_ERROR_OBJECT(self, "Failed to link selector input: %d", result);
        return false;
    }
    return true;
}

static bool init_webrtc(GstOpkSrc *self) {
    self->webrtcbin = create_and_add_opksrc_element(self, "webrtcbin");
    if (!self->webrtcbin) {
        return false;
    }
    GstCaps *caps = gst_caps_from_string(
        "application/x-rtp, media=audio, encoding-name=OPUS, clock-rate=48000");
    GstWebRTCRTPTransceiver *transceiver = nullptr;
    g_signal_emit_by_name(self->webrtcbin,
                          "add-transceiver",
                          GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY,
                          caps,
                          &transceiver);
    gst_caps_unref(caps);
    if (!transceiver) {
        return false;
    }
    gst_object_unref(transceiver);
    self->pad_added_handler =
        g_signal_connect(self->webrtcbin, "pad-added", G_CALLBACK(gst_opk_src_pad_added), self);
    return true;
}

bool gst_opk_src_reset_webrtc(GstOpkSrc *self) {
    g_rec_mutex_lock(&self->media_lock);
    g_object_set(self->selector, "active-pad", self->silence_pad, nullptr);
    if (self->webrtcbin) {
        if (self->pad_added_handler) {
            g_signal_handler_disconnect(self->webrtcbin, self->pad_added_handler);
            self->pad_added_handler = 0;
        }
        gst_element_set_state(self->webrtcbin, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(self), self->webrtcbin);
        self->webrtcbin = nullptr;
    }
    const bool ready =
        init_webrtc(self) &&
        gst_element_set_state(self->webrtcbin, self->media_state) != GST_STATE_CHANGE_FAILURE;
    g_rec_mutex_unlock(&self->media_lock);
    return ready;
}

static bool init_audio(GstOpkSrc *self) {
    if (!init_webrtc(self)) {
        return false;
    }
    self->depay = create_and_add_opksrc_element(self, "rtpopusdepay");
    GstElement *decoder = create_and_add_opksrc_element(self, "opusdec");
    GstElement *convert = create_and_add_opksrc_element(self, "audioconvert");
    GstElement *resample = create_and_add_opksrc_element(self, "audioresample");
    GstElement *filter = create_and_add_opksrc_element(self, "capsfilter");
    self->output_filter = filter;
    GstElement *silence = create_and_add_opksrc_element(self, "audiotestsrc");
    self->selector = create_and_add_opksrc_element(self, "input-selector");
    if (!self->webrtcbin || !self->depay || !decoder || !convert || !resample || !filter ||
        !silence || !self->selector) {
        return false;
    }

    gst_util_set_object_arg(G_OBJECT(silence), "wave", "silence");
    g_object_set(silence, "is-live", TRUE, nullptr);
    g_object_set(self->selector, "sync-streams", TRUE, nullptr);
    gst_util_set_object_arg(G_OBJECT(self->selector), "sync-mode", "clock");

    update_output_caps(self);
    if (!gst_element_link_many(self->depay, decoder, convert, resample, nullptr) ||
        !gst_element_link(self->selector, filter)) {
        GST_ERROR_OBJECT(self, "Failed to link audio receive chain");
        return false;
    }

    if (!link_selector_input(self, silence, &self->silence_pad) ||
        !link_selector_input(self, resample, &self->audio_pad)) {
        return false;
    }
    g_object_set(self->selector, "active-pad", self->silence_pad, nullptr);

    GstPad *target = gst_element_get_static_pad(filter, "src");
    GstPadTemplate *pad_template = gst_static_pad_template_get(&src_template);
    GstPad *ghost_pad = gst_ghost_pad_new_from_template("src", target, pad_template);
    gst_object_unref(pad_template);
    gst_object_unref(target);
    if (!ghost_pad) {
        GST_ERROR_OBJECT(self, "Failed to create audio source pad");
        return false;
    }
    if (!gst_element_add_pad(GST_ELEMENT(self), ghost_pad)) {
        gst_object_unref(ghost_pad);
        GST_ERROR_OBJECT(self, "Failed to add audio source pad");
        return false;
    }

    return true;
}

static GstStateChangeReturn gst_opk_src_change_state(GstElement *element,
                                                     GstStateChange transition) {
    auto *self = reinterpret_cast<GstOpkSrc *>(element);
    if (transition == GST_STATE_CHANGE_NULL_TO_READY && !self->initialized) {
        GST_ELEMENT_ERROR(self, CORE, FAILED, ("OpkSrc initialization failed"), (nullptr));
        return GST_STATE_CHANGE_FAILURE;
    }
    g_rec_mutex_lock(&self->media_lock);
    self->media_state = GST_STATE_TRANSITION_NEXT(transition);
    g_rec_mutex_unlock(&self->media_lock);
    if (transition == GST_STATE_CHANGE_READY_TO_NULL) {
        self->signaling->stop();
    }

    g_rec_mutex_lock(&self->media_lock);
    auto result = GST_ELEMENT_CLASS(gst_opk_src_parent_class)->change_state(element, transition);
    g_rec_mutex_unlock(&self->media_lock);

    if (transition == GST_STATE_CHANGE_NULL_TO_READY && result != GST_STATE_CHANGE_FAILURE &&
        !self->signaling->start()) {
        GST_ELEMENT_ERROR(self,
                          RESOURCE,
                          OPEN_READ_WRITE,
                          ("Unable to start WebRTC signaling on port %d", self->ws_port),
                          (nullptr));
        result = GST_STATE_CHANGE_FAILURE;
    }
    return result;
}

static void gst_opk_src_dispose(GObject *object) {
    auto *self = reinterpret_cast<GstOpkSrc *>(object);
    if (self->signaling) {
        self->signaling->stop();
        delete self->signaling;
        self->signaling = nullptr;
    }
    if (self->pad_added_handler) {
        g_signal_handler_disconnect(self->webrtcbin, self->pad_added_handler);
        self->pad_added_handler = 0;
    }
    if (self->silence_pad) {
        gst_element_release_request_pad(self->selector, self->silence_pad);
        gst_clear_object(&self->silence_pad);
    }
    if (self->audio_pad) {
        gst_element_release_request_pad(self->selector, self->audio_pad);
        gst_clear_object(&self->audio_pad);
    }
    gst_opk_src_dispose_video(self);
    G_OBJECT_CLASS(gst_opk_src_parent_class)->dispose(object);
}

static void gst_opk_src_finalize(GObject *object) {
    auto *self = reinterpret_cast<GstOpkSrc *>(object);
    g_rec_mutex_clear(&self->media_lock);
    G_OBJECT_CLASS(gst_opk_src_parent_class)->finalize(object);
}

static void gst_opk_src_set_property(GObject *object,
                                     guint property_id,
                                     const GValue *value,
                                     GParamSpec *pspec) {
    auto *self = reinterpret_cast<GstOpkSrc *>(object);

    switch (property_id) {
    case PROP_WS_PORT:
        self->ws_port = g_value_get_int(value);
        break;
    case PROP_RATE:
        GST_STATE_LOCK(self);
        if (GST_STATE(self) == GST_STATE_NULL) {
            self->rate = g_value_get_int(value);
            update_output_caps(self);
        } else {
            GST_WARNING_OBJECT(self, "Set rate while opksrc is in NULL state");
        }
        GST_STATE_UNLOCK(self);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void
gst_opk_src_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    auto *self = reinterpret_cast<GstOpkSrc *>(object);

    switch (property_id) {
    case PROP_WS_PORT:
        g_value_set_int(value, self->ws_port);
        break;
    case PROP_RATE:
        g_value_set_int(value, self->rate);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void gst_opk_src_init(GstOpkSrc *self) {
    g_rec_mutex_init(&self->media_lock);
    self->media_state = GST_STATE_NULL;
    self->ws_port = 8003;
    self->rate = 16000;
    self->signaling = new OpkSrcSignaling(self);
    self->initialized = init_audio(self) && gst_opk_src_init_video(self);
}

static void gst_opk_src_class_init(GstOpkSrcClass *klass) {
    auto *gobject_class = G_OBJECT_CLASS(klass);
    auto *element_class = GST_ELEMENT_CLASS(klass);

    gobject_class->set_property = gst_opk_src_set_property;
    gobject_class->get_property = gst_opk_src_get_property;
    gobject_class->dispose = gst_opk_src_dispose;
    gobject_class->finalize = gst_opk_src_finalize;
    element_class->change_state = gst_opk_src_change_state;
    gst_element_class_add_static_pad_template(element_class, &src_template);
    gst_opk_src_video_class_init(element_class);

    constexpr GParamFlags kRW =
        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_property(gobject_class,
                                    PROP_WS_PORT,
                                    g_param_spec_int("ws-port",
                                                     "WebSocket Port",
                                                     "WebSocket port for WebRTC signaling",
                                                     1,
                                                     65535,
                                                     8003,
                                                     kRW));

    g_object_class_install_property(gobject_class,
                                    PROP_RATE,
                                    g_param_spec_int("rate",
                                                     "Sample rate",
                                                     "Output PCM sample rate in Hz (set in NULL)",
                                                     1,
                                                     G_MAXINT,
                                                     16000,
                                                     kRW));

    gst_element_class_set_static_metadata(element_class,
                                          "OpkSrc",
                                          "Source/Audio/Network/Bin",
                                          "Receives WebRTC Opus audio as mono F32LE PCM",
                                          "Arm Limited <perception-fdbck@arm.com>");
}

static gboolean opksrc_plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "opksrc", GST_RANK_NONE, GST_TYPE_OPK_SRC);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  opksrc,
                  "OpkSrc bin: WebRTC Opus to raw audio",
                  opksrc_plugin_init,
                  "1.0",
                  "Apache 2.0",
                  PACKAGE,
                  "https://www.arm.com/")
