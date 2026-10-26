#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Exercise default silence and the receive chain without WebRTC signaling."""

from pathlib import Path
import socket
import sys

from pipeline_test_utils import load_gstreamer_plugins, release_pipeline


def make_pipeline(gst, rate):
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    pipeline = gst.parse_launch(
        f"opksrc name=source ws-port={port} ! appsink name=output sync=false"
    )
    source = pipeline.get_by_name("source")
    assert source.get_property("rate") == 16000
    if rate != 16000:
        source.set_property("rate", rate)
    assert source.get_property("rate") == rate
    return pipeline


def check_silence(gst, rate):
    pipeline = make_pipeline(gst, rate)
    try:
        source = pipeline.get_by_name("source")
        selector = source.get_by_name("input-selector")
        silence = source.get_by_name("audiotestsrc")
        assert selector.get_property("active-pad") == silence.get_static_pad("src").get_peer()
        expected = gst.Caps.from_string(
            f"audio/x-raw,format=F32LE,layout=interleaved,rate={rate},channels=1"
        )
        output = pipeline.get_by_name("output")
        for _ in range(2):
            assert pipeline.set_state(gst.State.PLAYING) != gst.StateChangeReturn.FAILURE
            previous_pts = None
            for _ in range(3):
                sample = output.emit("try-pull-sample", 5 * gst.SECOND)
                assert sample is not None, "No silence received without a browser"
                assert sample.get_caps().is_subset(expected), sample.get_caps().to_string()
                buffer = sample.get_buffer()
                assert buffer.get_size() > 0
                assert not any(buffer.extract_dup(0, buffer.get_size()))
                assert buffer.pts != gst.CLOCK_TIME_NONE
                if previous_pts is not None:
                    assert buffer.pts > previous_pts
                previous_pts = buffer.pts
            release_pipeline(pipeline, gst)
    finally:
        release_pipeline(pipeline, gst)


def check_received_audio(gst, rate):
    pipeline = make_pipeline(gst, rate)
    try:
        source = pipeline.get_by_name("source")
        webrtc = source.get_by_name("webrtcbin")
        assert webrtc is not None
        assert source.get_by_name("opusdec") is not None
        selector = source.get_by_name("input-selector")
        audio_pad = source.get_by_name("audioresample").get_static_pad("src").get_peer()
        selector.set_property("active-pad", audio_pad)
        sender = gst.parse_bin_from_description(
            "audiotestsrc num-buffers=20 ! "
            "audio/x-raw,rate=48000,channels=2 ! opusenc ! rtpopuspay ! "
            'capsfilter caps="application/x-rtp,media=audio,encoding-name=OPUS,clock-rate=48000"',
            True,
        )
        webrtc.add(sender)
        incoming_pad = gst.GhostPad.new("src_test", sender.get_static_pad("src"))
        assert webrtc.add_pad(incoming_pad)
        assert incoming_pad.is_linked()
        output = pipeline.get_by_name("output")
        expected = gst.Caps.from_string(
            f"audio/x-raw,format=F32LE,layout=interleaved,rate={rate},channels=1"
        )
        for _ in range(2):
            assert pipeline.set_state(gst.State.PLAYING) != gst.StateChangeReturn.FAILURE
            sample = output.emit("try-pull-sample", 5 * gst.SECOND)
            assert sample is not None, "No decoded audio received"
            assert sample.get_caps().is_subset(expected), sample.get_caps().to_string()
            assert sample.get_buffer().get_size() > 0
            assert sample.get_buffer().get_size() % 4 == 0
            buffer = sample.get_buffer()
            assert any(buffer.extract_dup(0, buffer.get_size())), "Expected decoded tone"
            release_pipeline(pipeline, gst)
    finally:
        release_pipeline(pipeline, gst)


if __name__ == "__main__":
    gst = load_gstreamer_plugins([Path(sys.argv[1])])
    for rate in (16000, 48000, 44100):
        check_silence(gst, rate)
        check_received_audio(gst, rate)
