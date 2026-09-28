/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

#ifndef __WEBRTC_SESSION_H__
#define __WEBRTC_SESSION_H__

#include <atomic>
#include <cstddef>

#include <gst/gst.h>

class OpkSinkWebRtcSession {
  public:
    GstElement *owner_bin = nullptr;
    GstElement *video_tee = nullptr;
    GstElement *audio_tee = nullptr;

    gulong onn_id = 0;
    gulong oic_id = 0;

    GstElement *webrtcbin = nullptr;
    GstPad *webrtc_sink_pad = nullptr;
    GstPad *audio_webrtc_sink_pad = nullptr;

    GstElement *queue = nullptr;
    GstElement *audio_queue = nullptr;

    GstPad *tee_src_pad = nullptr;
    GstPad *audio_tee_src_pad = nullptr;

    GstElement *a_capsfilter = nullptr;
    GstElement *v_capsfilter = nullptr;

    GstElement *v_pay = nullptr;
    GstElement *a_pay = nullptr;

    gint pt_video_vp8 = 96;
    gint pt_audio_opus = 111;

    OpkSinkWebRtcSession() = default;
    OpkSinkWebRtcSession(GstElement *owner_bin, GstElement *video_tee, GstElement *audio_tee);

    OpkSinkWebRtcSession(const OpkSinkWebRtcSession &) = delete;
    OpkSinkWebRtcSession &operator=(const OpkSinkWebRtcSession &) = delete;

    OpkSinkWebRtcSession(OpkSinkWebRtcSession &&rhs) = delete;
    OpkSinkWebRtcSession &operator=(OpkSinkWebRtcSession &&) = delete;

    ~OpkSinkWebRtcSession() noexcept;

    void cleanup() noexcept;
    bool cleaned_up() const;
    std::size_t active_resource_count() const;

  private:
    std::atomic_bool cleaned_up_{false};

    void disconnect_signals() noexcept;
};

#endif // !__WEBRTC_SESSION_H__
