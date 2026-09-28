#!/usr/bin/env python3
################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################

"""Exercise valid and invalid messages through the control and WebRTC sockets."""

from collections import deque
import json
from pathlib import Path
import sys
from time import monotonic

import gi
from gi.repository import GLib, GObject

from pipeline_test_utils import load_gstreamer_plugins, release_pipeline


def wait_for(predicate, context="WebSocket activity"):
    deadline = monotonic() + 5
    timer = GLib.timeout_add(5000, lambda: True)
    try:
        while not predicate():
            assert monotonic() < deadline, f"Timed out waiting for {context}"
            GLib.MainContext.default().iteration(True)
    finally:
        GLib.source_remove(timer)


def connect_websocket(session, port, context="WebSocket connection"):
    from gi.repository import Soup

    request = Soup.Message.new("GET", f"ws://127.0.0.1:{port}/")
    connected = []
    session.websocket_connect_async(
        request, None, None, 0, None, lambda _, result: connected.append(result)
    )
    wait_for(lambda: connected, context)
    return session.websocket_connect_finish(connected[0])


def create_sdp_offer(gst):
    gi.require_version("GstWebRTC", "1.0")
    from gi.repository import GstWebRTC

    peer = gst.ElementFactory.make("webrtcbin")
    try:
        for media, codec, rate, payload in (
            ("video", "VP8", 90000, 96),
            ("audio", "OPUS", 48000, 97),
        ):
            caps = gst.Caps.from_string(
                f"application/x-rtp,media={media},encoding-name={codec},"
                f"clock-rate={rate},payload={payload}"
            )
            peer.emit("add-transceiver", GstWebRTC.WebRTCRTPTransceiverDirection.RECVONLY, caps)
        peer.set_state(gst.State.PLAYING)
        promise = gst.Promise.new()
        peer.emit("create-offer", None, promise)
        assert promise.wait() == gst.PromiseResult.REPLIED
        reply = promise.get_reply()
        description = reply.get_value("offer")
        return description.sdp.as_text()
    finally:
        peer.set_state(gst.State.NULL)


def check_control(session, pipeline, gst):
    sink = pipeline.get_by_name("sink")
    model = pipeline.get_by_name("model")
    websocket = connect_websocket(session, sink.get_property("ctrl-port"))
    reports = deque()
    websocket.connect("message", lambda _, kind, data: reports.append(
        json.loads(data.get_data())["pipeline_state"]["playing"]
    ))

    def check_play_pause_after(message):
        for playing in (False, True):
            websocket.send_text('{"type":"play_pause"}')
            wait_for(lambda: reports, f"pipeline state after {message!r}")
            while reports.popleft() != playing:
                wait_for(lambda: reports, f"pipeline state after {message!r}")
            assert pipeline.get_state(0)[1] == (
                gst.State.PLAYING if playing else gst.State.PAUSED
            ), f"Unexpected pipeline state after {message!r}"

    try:
        # JSON
        message = "{"
        websocket.send_text(message)
        check_play_pause_after(message)

        # type
        message = '{"type":1}'
        websocket.send_text(message)
        check_play_pause_after(message)

        # name: valid toggles the model; invalid leaves it unchanged.
        valid = json.dumps({"type": "model_toggle", "name": "model"})
        invalid = json.dumps({"type": "model_toggle", "name": "model\0extra"})
        websocket.send_text(valid)
        check_play_pause_after(valid)
        assert model.props.active, f"Unexpected model state after {valid!r}"
        websocket.send_text(invalid)
        check_play_pause_after(invalid)
        assert model.props.active, f"Unexpected model state after {invalid!r}"
    finally:
        websocket.close(1000, "done")


def check_webrtc_message(session, sink, message, *, expect_answer=False, offer_after=None):
    websocket = connect_websocket(
        session, sink.get_property("ws-port"), f"WebSocket connection for {message!r}"
    )
    removed, received = [], []
    websocket.connect("message", lambda _, kind, data: received.append(
        json.loads(data.get_data())["type"]
    ))

    def on_removed(_, element):
        if element.get_factory().get_name() == "webrtcbin":
            GLib.idle_add(removed.append, True)

    handler = sink.connect("element-removed", on_removed)
    try:
        websocket.send_text(message)
        if offer_after:
            # ICE has no reply. The next offer must still get an answer.
            websocket.send_text(offer_after)
        wait_for(lambda: removed or "answer" in received, f"response to {message!r}")
        if expect_answer:
            assert not removed, f"Unexpected session cleanup after {message!r}"
        else:
            assert removed, f"Missing session cleanup after {message!r}"
    finally:
        websocket.close(1000, "done")
        wait_for(lambda: removed, f"session cleanup after {message!r}")
        sink.disconnect(handler)


def check_webrtc(session, pipeline, gst):
    sink = pipeline.get_by_name("sink")
    sdp = create_sdp_offer(gst)
    offer = json.dumps({"type": "offer", "sdp": sdp})
    ice = {"candidate": "candidate:1 1 UDP 2122260223 127.0.0.1 9 typ host", "sdpMLineIndex": 0}

    # JSON
    valid = offer
    invalid = "{"
    check_webrtc_message(session, sink, valid, expect_answer=True)
    check_webrtc_message(session, sink, invalid)

    # type
    valid = offer
    invalid = json.dumps({"type": 1, "sdp": sdp})
    check_webrtc_message(session, sink, valid, expect_answer=True)
    check_webrtc_message(session, sink, invalid)

    # sdp
    valid = offer
    invalid = json.dumps({"type": "offer", "sdp": sdp + "\0"})
    check_webrtc_message(session, sink, valid, expect_answer=True)
    check_webrtc_message(session, sink, invalid)

    # ice
    valid = json.dumps({"type": "candidate", "ice": ice})
    invalid = json.dumps({"type": "candidate", "ice": None})
    check_webrtc_message(session, sink, valid, expect_answer=True, offer_after=offer)
    check_webrtc_message(session, sink, invalid, offer_after=offer)

    # ice.candidate
    valid = json.dumps({"type": "candidate", "ice": ice})
    invalid = json.dumps(
        {"type": "candidate", "ice": {**ice, "candidate": ice["candidate"] + "\r\n"}}
    )
    check_webrtc_message(session, sink, valid, expect_answer=True, offer_after=offer)
    check_webrtc_message(session, sink, invalid, offer_after=offer)

    # ice.sdpMLineIndex
    valid = json.dumps({"type": "candidate", "ice": ice})
    invalid = json.dumps({"type": "candidate", "ice": {**ice, "sdpMLineIndex": -1}})
    check_webrtc_message(session, sink, valid, expect_answer=True, offer_after=offer)
    check_webrtc_message(session, sink, invalid, offer_after=offer)

    assert pipeline.get_state(0)[1] == gst.State.PLAYING, (
        f"Unexpected pipeline state after {invalid!r}"
    )


def check_messages():
    gi.require_version("Soup", "3.0")
    from gi.repository import Soup

    plugin, endpoint = sys.argv[1:]
    gst = load_gstreamer_plugins([Path(plugin)])
    pipeline = gst.parse_launch(
        "videotestsrc is-live=true ! "
        "video/x-raw,format=I420,width=160,height=120,framerate=5/1 ! opksink name=sink"
    )
    if endpoint == "control":
        class Model(gst.Bin):
            active = GObject.Property(type=bool, default=False)

        pipeline.add(Model(name="model"))
    session = Soup.Session()
    try:
        assert pipeline.set_state(gst.State.PLAYING) != gst.StateChangeReturn.FAILURE
        assert pipeline.get_state(5 * gst.SECOND)[1] == gst.State.PLAYING
        if endpoint == "control":
            check_control(session, pipeline, gst)
        else:
            check_webrtc(session, pipeline, gst)
    finally:
        session.abort()
        release_pipeline(pipeline, gst)


if __name__ == "__main__":
    check_messages()
