#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Exercise invalid messages through the real control and WebRTC sockets."""

from collections import deque
import json
from pathlib import Path
import sys
from time import monotonic

import gi
from gi.repository import GLib

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


def check_messages():
    gi.require_version("Soup", "3.0")
    from gi.repository import Soup

    plugin, endpoint = sys.argv[1:]
    gst = load_gstreamer_plugins([Path(plugin)])
    pipeline = gst.parse_launch(
        "videotestsrc is-live=true ! "
        "video/x-raw,format=I420,width=160,height=120,framerate=5/1 ! opksink name=sink"
    )
    session, websocket = Soup.Session(), None
    try:
        assert pipeline.set_state(gst.State.PLAYING) != gst.StateChangeReturn.FAILURE
        assert pipeline.get_state(5 * gst.SECOND)[1] == gst.State.PLAYING
        sink = pipeline.get_by_name("sink")
        port = sink.get_property("ctrl-port" if endpoint == "control" else "ws-port")
        if endpoint == "control":
            request = Soup.Message.new("GET", f"ws://127.0.0.1:{port}/")
            connected = []
            session.websocket_connect_async(
                request, None, None, 0, None, lambda _, result: connected.append(result)
            )
            wait_for(lambda: connected)
            websocket = session.websocket_connect_finish(connected[0])
            reports = deque()
            websocket.connect("message", lambda _, kind, data: reports.append(
                json.loads(data.get_data())["pipeline_state"]["playing"]
            ))
            for message in ("{", "{}", '{"type":1}', "[]", '{"type":"model_toggle"}',
                            '{"type":"model_toggle","name":1}',
                            '{"type":"model_toggle","name":"sink\\u0000extra"}'):
                websocket.send_text(message)
                for playing in (False, True):
                    websocket.send_text('{"type":"play_pause"}')
                    wait_for(lambda: reports, f"pipeline state after {message!r}")
                    while reports.popleft() != playing:
                        wait_for(lambda: reports, f"pipeline state after {message!r}")
                    assert pipeline.get_state(0)[1] == (
                        gst.State.PLAYING if playing else gst.State.PAUSED
                    ), f"Unexpected pipeline state after {message!r}"
        else:
            gi.require_version("GstWebRTC", "1.0")
            from gi.repository import GstWebRTC

            peer = gst.ElementFactory.make("webrtcbin")
            try:
                for media, codec, rate, payload in (("video", "VP8", 90000, 96),
                                                    ("audio", "OPUS", 48000, 97)):
                    peer.emit("add-transceiver", GstWebRTC.WebRTCRTPTransceiverDirection.RECVONLY,
                              gst.Caps.from_string(f"application/x-rtp,media={media},"
                                                   f"encoding-name={codec},clock-rate={rate},"
                                                   f"payload={payload}"))
                peer.set_state(gst.State.PLAYING)
                promise = gst.Promise.new()
                peer.emit("create-offer", None, promise)
                assert promise.wait() == gst.PromiseResult.REPLIED
                reply = promise.get_reply()
                description = reply.get_value("offer")
                sdp = description.sdp.as_text()
            finally:
                peer.set_state(gst.State.NULL)

            offer = json.dumps({"type": "offer", "sdp": sdp})
            for message in (offer, json.dumps({"type": "offer", "sdp": sdp + "\0"}),
                            "{", '{"type":"offer"}', '{"type":"candidate"}',
                            '{"type":"candidate","ice":{}}',
                            *[json.dumps({"type": "candidate", "ice": {
                                "candidate": "", "sdpMLineIndex": index
                            }}) for index in (-1, 1.5, 4294967296)],
                            *[json.dumps({"type": "candidate", "ice": {
                                "candidate": candidate, "sdpMLineIndex": 0
                            }}) for candidate in ("line\r\nnext", "line\0next")]):
                request = Soup.Message.new("GET", f"ws://127.0.0.1:{port}/")
                connected = []
                session.websocket_connect_async(
                    request, None, None, 0, None, lambda _, result: connected.append(result)
                )
                wait_for(lambda: connected, f"WebSocket connection for {message!r}")
                websocket = session.websocket_connect_finish(connected[0])
                removed = []
                received = []
                websocket.connect("message", lambda _, kind, data: received.append(
                    json.loads(data.get_data())["type"]
                ))

                def on_removed(_, element):
                    if element.get_factory().get_name() == "webrtcbin":
                        GLib.idle_add(removed.append, True)

                handler = sink.connect("element-removed", on_removed)
                websocket.send_text(message)
                wait_for(lambda: removed or "answer" in received, f"response to {message!r}")
                if message == offer:
                    assert not removed, f"Unexpected session cleanup after {message!r}"
                else:
                    assert removed, f"Missing session cleanup after {message!r}"
                websocket.close(1000, "done")
                wait_for(lambda: removed, f"session cleanup after {message!r}")
                sink.disconnect(handler)
                websocket = None
        assert pipeline.get_state(0)[1] == gst.State.PLAYING, (
            f"Unexpected pipeline state after {message!r}"
        )
    finally:
        if websocket:
            websocket.close(1000, "done")
        session.abort()
        release_pipeline(pipeline, gst)


if __name__ == "__main__":
    check_messages()
