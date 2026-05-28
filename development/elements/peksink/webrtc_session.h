/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#ifndef __WEBRTC_SESSION_H__
#define __WEBRTC_SESSION_H__

#include <atomic>
#include <cstddef>

#include <gst/gst.h>

class PekSinkWebRtcSession {
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

    PekSinkWebRtcSession() = default;
    PekSinkWebRtcSession(GstElement *owner_bin, GstElement *video_tee, GstElement *audio_tee);

    PekSinkWebRtcSession(const PekSinkWebRtcSession &) = delete;
    PekSinkWebRtcSession &operator=(const PekSinkWebRtcSession &) = delete;

    PekSinkWebRtcSession(PekSinkWebRtcSession &&rhs) = delete;
    PekSinkWebRtcSession &operator=(PekSinkWebRtcSession &&) = delete;

    ~PekSinkWebRtcSession();

    void cleanup();
    bool cleaned_up() const;
    std::size_t active_resource_count() const;

  private:
    std::atomic_bool cleaned_up_{false};

    void disconnect_signals();
};

#endif // !__WEBRTC_SESSION_H__
