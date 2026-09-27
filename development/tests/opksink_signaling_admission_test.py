#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Bound WebRTC sessions and reclaim clients that never send an offer."""

import json
from pathlib import Path
import sys

import gi

from opksink_websocket_json_test import connect_websocket, create_sdp_offer, wait_for
from pipeline_test_utils import load_gstreamer_plugins, release_pipeline


SESSION_LIMIT = 16


def valid_session(session, port, offer):
    websocket = connect_websocket(session, port)
    messages = []
    websocket.connect(
        "message", lambda _, kind, data: messages.append(json.loads(data.get_data())["type"])
    )
    websocket.send_text(json.dumps({"type": "offer", "sdp": offer}))
    wait_for(lambda: "answer" in messages, "valid WebRTC answer")
    return websocket


def main():
    gi.require_version("Soup", "3.0")
    from gi.repository import Soup

    gst = load_gstreamer_plugins([Path(sys.argv[1])])
    pipeline = gst.parse_launch(
        "videotestsrc is-live=true ! "
        "video/x-raw,format=I420,width=160,height=120,framerate=5/1 ! opksink name=sink"
    )
    session = Soup.Session()
    clients = []
    try:
        assert pipeline.set_state(gst.State.PLAYING) != gst.StateChangeReturn.FAILURE
        assert pipeline.get_state(5 * gst.SECOND)[1] == gst.State.PLAYING
        sink = pipeline.get_by_name("sink")
        port = sink.get_property("ws-port")
        offer = create_sdp_offer(gst)
        added, removed = [], []

        def remember(target, element):
            if element.get_factory() and element.get_factory().get_name() == "webrtcbin":
                target.append(element.get_name())

        sink.connect("element-added", lambda _, element: remember(added, element))
        sink.connect("element-removed", lambda _, element: remember(removed, element))

        baseline = valid_session(session, port, offer)
        baseline.close(1000, "baseline complete")
        wait_for(lambda: len(removed) == 1, "baseline cleanup")

        clients = [connect_websocket(session, port) for _ in range(SESSION_LIMIT)]
        wait_for(lambda: len(added) == SESSION_LIMIT + 1, "admitted session allocation")

        rejected = connect_websocket(session, port)
        wait_for(
            lambda: rejected.get_state() == Soup.WebsocketState.CLOSED,
            "session-limit rejection",
        )
        assert rejected.get_close_code() == 1013
        assert len(added) == SESSION_LIMIT + 1, "rejected client allocated a webrtcbin"

        clients.pop().close(1000, "capacity reclamation")
        wait_for(lambda: len(removed) == 2, "closed session cleanup")
        clients.append(connect_websocket(session, port))
        wait_for(lambda: len(added) == SESSION_LIMIT + 2, "reclaimed admission slot")

        wait_for(
            lambda: len(removed) == SESSION_LIMIT + 2
            and all(client.get_state() == Soup.WebsocketState.CLOSED for client in clients),
            "offer-timeout cleanup",
            timeout=15,
        )

        recovery = valid_session(session, port, offer)
        recovery.close(1000, "recovery complete")
        wait_for(lambda: len(removed) == SESSION_LIMIT + 3, "recovery cleanup")
        assert pipeline.get_state(0)[1] == gst.State.PLAYING
    finally:
        for client in clients:
            if client.get_state() != Soup.WebsocketState.CLOSED:
                client.close(1000, "test cleanup")
        session.abort()
        release_pipeline(pipeline, gst)


if __name__ == "__main__":
    main()
