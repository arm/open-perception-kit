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

"""Exercise valid and invalid messages through the control and WebRTC sockets."""

from collections import deque
from contextlib import ExitStack
import json
from pathlib import Path
import sys
from threading import Event
from time import monotonic, sleep

import gi
from gi.repository import GLib, GObject

from pipeline_test_utils import load_gstreamer_plugins, release_pipeline
from opksink_configured_ports_test import reserve_ports

MODEL_TOGGLE_BURST_SIZE = 16
CROSS_MODEL_TOGGLE_PAIR_COUNT = 100
REPORT_SETTLE_SECONDS = 0.5


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
    from gi.repository import Soup

    sink = pipeline.get_by_name("sink")
    model = pipeline.get_by_name("model")
    websocket = connect_websocket(session, sink.get_property("ctrl-port"))
    applied_values = []
    model_handler = model.connect(
        "notify::active", lambda owner, _spec: GLib.idle_add(applied_values.append, owner.props.active)
    )
    reports = deque()
    websocket.connect("message", lambda _, kind, data: reports.append(
        json.loads(data.get_data())["pipeline_state"]
    ))

    def check_playback_after(message):
        for state in ("PAUSED", "PLAYING", "READY", "PLAYING"):
            reports.clear()
            websocket.send_text(json.dumps({"type": "change_pipeline_state", "state": state}))
            wait_for(lambda: reports, f"pipeline state after {message!r}")
            while True:
                report = reports.popleft()
                if report["state"] == state and not report["pending"] and not report["busy"]:
                    break
                wait_for(lambda: reports, f"pipeline state after {message!r}")
            assert pipeline.get_state(0)[1] == getattr(gst.State, state), (
                f"Unexpected pipeline state after {message!r}"
            )
            if state == "PLAYING":
                decoder = pipeline.get_by_name("decoder")
                wait_for(
                    lambda: decoder.srcpads and all(pad.is_linked() for pad in decoder.srcpads),
                    "recreated decoder output connection",
                )

    try:
        # JSON
        message = "{"
        websocket.send_text(message)
        check_playback_after(message)

        # type
        message = '{"type":1}'
        websocket.send_text(message)
        check_playback_after(message)

        # name: valid toggles the model; invalid leaves it unchanged.
        valid = json.dumps({"type": "model_toggle", "name": "model"})
        invalid = json.dumps({"type": "model_toggle", "name": "model\0extra"})
        websocket.send_text(valid)
        wait_for(lambda: applied_values, "model toggle")
        check_playback_after(valid)
        assert model.props.active, f"Unexpected model state after {valid!r}"
        websocket.send_text(invalid)
        check_playback_after(invalid)
        assert model.props.active, f"Unexpected model state after {invalid!r}"

        bindings = [{"consumerId": "crop", "inputId": "input",
                     "payloadType": "open_perception_kit.metadata.BoxDetections",
                     "producerId": "detector/output"}]
        binding_update = json.dumps({"type": "model_inputs", "name": "model", "bindings": bindings})
        websocket.send_text(binding_update)
        check_playback_after(binding_update)
        assert json.loads(model.props.input_bindings) == bindings
        for update in ({"name": "model\0extra", "bindings": []},
                       {"name": "model", "bindings": "invalid"},
                       {"name": "model", "bindings": [bindings[0]] * 129}):
            invalid = json.dumps({"type": "model_inputs", **update})
            websocket.send_text(invalid)
            check_playback_after(invalid)
            assert json.loads(model.props.input_bindings) == bindings

        applied_values.clear()
        for _ in range(MODEL_TOGGLE_BURST_SIZE):
            websocket.send_text(valid)
        wait_for(lambda: len(applied_values) == MODEL_TOGGLE_BURST_SIZE, "model toggle burst")
        assert applied_values == [bool(index % 2) for index in range(MODEL_TOGGLE_BURST_SIZE)]

        dependent_model = pipeline.get_by_name("dependent-model")
        applied_models = []

        def record_model_change(owner, _spec):
            GLib.idle_add(applied_models.append, owner.get_name())

        model_order_handler = model.connect("notify::active", record_model_change)
        dependent_order_handler = dependent_model.connect("notify::active", record_model_change)
        dependent_toggle = json.dumps({"type": "model_toggle", "name": "dependent-model"})
        try:
            for _ in range(CROSS_MODEL_TOGGLE_PAIR_COUNT):
                websocket.send_text(valid)
                websocket.send_text(dependent_toggle)
            expected_models = ["model", "dependent-model"] * CROSS_MODEL_TOGGLE_PAIR_COUNT
            wait_for(lambda: len(applied_models) == len(expected_models), "ordered model toggles")
            assert applied_models == expected_models, applied_models

            def activate_provider(owner, _spec):
                if owner.props.active:
                    model.props.active = True

            dependency_handler = dependent_model.connect("notify::active", activate_provider)
            try:
                # Disabling the provider before enabling its consumer must reactivate the provider.
                applied_models.clear()
                websocket.send_text(valid)
                websocket.send_text(dependent_toggle)
                expected_models = ["model", "dependent-model", "model"]
                wait_for(lambda: len(applied_models) == len(expected_models), "provider activation")
                assert applied_models == expected_models, applied_models
                assert model.props.active and dependent_model.props.active
            finally:
                dependent_model.disconnect(dependency_handler)
        finally:
            model.disconnect(model_order_handler)
            dependent_model.disconnect(dependent_order_handler)

        stopped = []

        def stop_on_activation(_model, _spec):
            pipeline.set_state(gst.State.NULL)
            GLib.idle_add(stopped.append, True)

        stop_handler = model.connect("notify::active", stop_on_activation)
        try:
            websocket.send_text(valid)
            wait_for(lambda: stopped, "shutdown from a model property callback")
            assert pipeline.get_state(0)[1] == gst.State.NULL
        finally:
            model.disconnect(stop_handler)
    finally:
        model.disconnect(model_handler)
        if websocket.get_state() == Soup.WebsocketState.OPEN:
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


def check_model_edit_lifecycle(gst, port_properties):
    from gi.repository import Soup

    # Both editors discover the same real opkinfer before either changes the topology.
    for stop_on_removal in (False, True):
        with ExitStack() as reservations:
            sink_names = ("first", "second")
            ports, _ = reserve_ports(reservations, len(port_properties) * len(sink_names))
            pipeline = gst.parse_launch(
                "videotestsrc name=source ! opkinfer name=shared-model ! tee name=split "
                "split. ! queue ! opksink name=first "
                "split. ! queue ! opksink name=second"
            )
            sinks = [pipeline.get_by_name(name) for name in sink_names]
            for index, sink in enumerate(sinks):
                offset = index * len(port_properties)
                for name, port in zip(port_properties, ports[offset:]):
                    sink.set_property(name, port)
        session = Soup.Session()
        reports = [[], []]
        stopped = Event()
        try:
            assert pipeline.set_state(gst.State.READY) != gst.StateChangeReturn.FAILURE
            sockets = []
            for index, sink in enumerate(sinks):
                websocket = connect_websocket(session, sink.get_property("ctrl-port"))
                websocket.connect("message", lambda _, kind, data, index=index:
                                  reports[index].append(json.loads(data.get_data())))
                sockets.append(websocket)
            wait_for(lambda: all(reports), "both model editor snapshots")
            requests = [
                {"type": "model_edit", "action": "remove", "modelGroup": "shared-model",
                 "name": "shared-model", "configurationId": report[-1]["model_editor"]["configurationId"]}
                for report in reports
            ]

            def stop_from_removal(_pipeline, element):
                if element.get_name() == "shared-model":
                    # Let periodic status reporting overlap the removal callback.
                    sleep(REPORT_SETTLE_SECONDS)
                    pipeline.set_state(gst.State.NULL)
                    stopped.set()

            if stop_on_removal:
                pipeline.connect("element-removed", stop_from_removal)
            sockets[0].send_text(json.dumps(requests[0]))
            if stop_on_removal:
                wait_for(stopped.is_set, "shutdown from model removal")
                assert pipeline.get_state(0)[1] == gst.State.NULL
            else:
                wait_for(lambda: any(
                    report["model_editor"]["configurationId"] > requests[0]["configurationId"] and
                    not report["pipeline_state"]["busy"] for report in reports[0]
                ), "first model removal")
                source = pipeline.get_by_name("source").get_static_pad("src")
                destination = pipeline.get_by_name("split").get_static_pad("sink")
                assert source.get_peer() == destination
                sockets[1].send_text(json.dumps(requests[1]))
                wait_for(lambda: any(
                    report["pipeline_state"].get("error") for report in reports[1]
                ), "stale model removal rejection")
                assert source.get_peer() == destination, "Stale edit disconnected the bypass"
                assert reports[1][-1]["model_editor"]["configurationId"] == requests[1]["configurationId"]
        finally:
            session.abort()
            release_pipeline(pipeline, gst)


def check_messages():
    gi.require_version("Soup", "3.0")
    from gi.repository import Soup

    plugin, endpoint, inference_plugin = sys.argv[1:]
    gst = load_gstreamer_plugins([Path(plugin), Path(inference_plugin)])
    port_properties = ("http-port", "ws-port", "ctrl-port")
    with ExitStack() as reservations:
        ports, _ = reserve_ports(reservations, len(port_properties))
        pipeline = gst.parse_launch(
            "videotestsrc is-live=true ! "
            "video/x-raw,format=I420,width=160,height=120,framerate=5/1 ! "
            "decodebin name=decoder ! opksink name=sink"
        )
        sink = pipeline.get_by_name("sink")
        for name, port in zip(port_properties, ports):
            sink.set_property(name, port)
    if endpoint == "control":
        class Model(gst.Bin):
            active = GObject.Property(type=bool, default=False)
            input_bindings = GObject.Property(type=str, default="[]")

        pipeline.add(Model(name="model"))
        pipeline.add(Model(name="dependent-model"))
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

    if endpoint == "control":
        check_model_edit_lifecycle(gst, port_properties)


if __name__ == "__main__":
    check_messages()
