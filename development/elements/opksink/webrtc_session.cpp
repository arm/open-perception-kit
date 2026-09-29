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

#include "webrtc_session.h"

#include <initializer_list>

#include <glib-object.h>
#include <gst/gstbin.h>
#include <gst/gstobject.h>
#include <gst/gstpad.h>

#include "utils.h"

namespace {

void unlink_peer(GstPad *pad) noexcept {
    if (!pad || !GST_IS_PAD(pad)) { // NOSONAR
        return;
    }

    GstPad *peer = gst_pad_get_peer(pad);
    if (!peer) {
        return;
    }

    if (gst_pad_get_direction(pad) == GST_PAD_SRC) {
        gst_pad_unlink(pad, peer);
    } else {
        gst_pad_unlink(peer, pad);
    }
    gst_object_unref(peer);
}

void remove_or_unref_element(GstElement *owner_bin, GstElement **element) noexcept {
    if (!element || !*element) {
        return;
    }

    // A concurrent parent state change must not restart an element being removed.
    gst_element_set_locked_state(*element, TRUE);
    gst_element_set_state(*element, GST_STATE_NULL);

    GstObject *parent = gst_object_get_parent(GST_OBJECT(*element));
    if (parent) {
        if (GST_IS_BIN(parent)) { // NOSONAR
            gst_bin_remove(GST_BIN(parent), *element);
        } else {
            g_debug("Per-client element parent is not a bin: %s", GST_ELEMENT_NAME(*element));
            gst_object_unref(*element);
        }
        gst_object_unref(parent);
    } else {
        gst_object_unref(*element);
    }

    *element = nullptr;
    (void)owner_bin;
}

std::size_t count_non_null(std::initializer_list<const void *> resources) {
    std::size_t count = 0;
    for (const void *resource : resources) {
        if (resource) {
            ++count;
        }
    }
    return count;
}

} // namespace

OpkSinkWebRtcSession::OpkSinkWebRtcSession(GstElement *owner_bin_,
                                           GstElement *video_tee_,
                                           GstElement *audio_tee_)
    : owner_bin(owner_bin_), video_tee(video_tee_), audio_tee(audio_tee_) {}

OpkSinkWebRtcSession::~OpkSinkWebRtcSession() noexcept {
    cleanup();
}

void OpkSinkWebRtcSession::cleanup() noexcept {
    if (cleaned_up_.exchange(true)) {
        return;
    }

    disconnect_signals();

    set_state_elements_many(
        GST_STATE_NULL, {webrtcbin, queue, v_pay, v_capsfilter, audio_queue, a_pay, a_capsfilter});

    unlink_peer(tee_src_pad);
    release_request_pad_and_unref(video_tee, &tee_src_pad);

    unlink_peer(audio_tee_src_pad);
    release_request_pad_and_unref(audio_tee, &audio_tee_src_pad);

    unlink_peer(webrtc_sink_pad);
    release_request_pad_and_unref(webrtcbin, &webrtc_sink_pad);

    unlink_peer(audio_webrtc_sink_pad);
    release_request_pad_and_unref(webrtcbin, &audio_webrtc_sink_pad);

    remove_or_unref_element(owner_bin, &audio_queue);
    remove_or_unref_element(owner_bin, &queue);
    remove_or_unref_element(owner_bin, &v_pay);
    remove_or_unref_element(owner_bin, &a_pay);
    remove_or_unref_element(owner_bin, &v_capsfilter);
    remove_or_unref_element(owner_bin, &a_capsfilter);
    remove_or_unref_element(owner_bin, &webrtcbin);
}

bool OpkSinkWebRtcSession::cleaned_up() const {
    return cleaned_up_.load();
}

std::size_t OpkSinkWebRtcSession::active_resource_count() const {
    return count_non_null({webrtcbin,
                           webrtc_sink_pad,
                           audio_webrtc_sink_pad,
                           queue,
                           audio_queue,
                           tee_src_pad,
                           audio_tee_src_pad,
                           a_capsfilter,
                           v_capsfilter,
                           v_pay,
                           a_pay});
}

void OpkSinkWebRtcSession::disconnect_signals() noexcept {
    if (!webrtcbin) {
        onn_id = 0;
        oic_id = 0;
        return;
    }

    if (onn_id && g_signal_handler_is_connected(webrtcbin, onn_id)) {
        g_signal_handler_disconnect(webrtcbin, onn_id);
    }
    if (oic_id && g_signal_handler_is_connected(webrtcbin, oic_id)) {
        g_signal_handler_disconnect(webrtcbin, oic_id);
    }

    onn_id = 0;
    oic_id = 0;
}
