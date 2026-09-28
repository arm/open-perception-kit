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
        request = Soup.Message.new("GET", f"ws://127.0.0.1:{port}/")
        connected = []
        session.websocket_connect_async(
            request, None, None, 0, None, lambda _, result: connected.append(result)
        )
        wait_for(lambda: connected)
        websocket = session.websocket_connect_finish(connected[0])
        if endpoint == "control":
            reports = deque()
            websocket.connect("message", lambda _, kind, data: reports.append(
                json.loads(data.get_data())["pipeline_state"]["playing"]
            ))
            for message in ("{", "{}", '{"type":1}', "[]"):
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
            removed = []

            def on_removed(_, element):
                if element.get_factory().get_name() == "webrtcbin":
                    GLib.idle_add(removed.append, True)

            sink.connect("element-removed", on_removed)
            message = "{"
            websocket.send_text(message)
            wait_for(lambda: removed, f"session cleanup after {message!r}")
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
