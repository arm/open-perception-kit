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

"""Negotiate real Opus RTP, enforce one client, and exercise signaling lifecycle."""

from pipeline_test_utils import load_gstreamer_plugins, release_pipeline
import json
from pathlib import Path
import socket
import sys
from threading import Event
from time import monotonic

import gi

gi.require_version("Soup", "3.0")
gi.require_version("GstWebRTC", "1.0")
gi.require_version("GstSdp", "1.0")

from gi.repository import GLib, Soup, GstWebRTC, GstSdp  # noqa: E402


def wait_for(predicate, description, timeout=10):
    deadline = monotonic() + timeout
    timer = GLib.timeout_add(10, lambda: True)
    try:
        while not predicate():
            assert monotonic() < deadline, description
            GLib.MainContext.default().iteration(True)
    finally:
        GLib.source_remove(timer)


def connect(session, port):
    results = []
    request = Soup.Message.new("GET", f"ws://127.0.0.1:{port}/ws")
    session.websocket_connect_async(
        request, None, None, 0, None, lambda _, result: results.append(result)
    )
    wait_for(lambda: results, "WebSocket connect")
    return session.websocket_connect_finish(results[0])


def invoke(gst, peer, action, argument):
    promise = gst.Promise.new()
    peer.emit(action, argument, promise)
    assert promise.wait() == gst.PromiseResult.REPLIED, action
    reply = promise.get_reply()
    assert reply is None or not reply.has_field("error"), str(reply)
    return reply.copy() if reply is not None else None


def check_stalled_peer(gst, source, websocket, sdp=None):
    timestamps = []
    pad = source.get_static_pad("src")

    def record_timestamp(_, info):
        timestamps.append(info.get_buffer().pts)
        return gst.PadProbeReturn.OK

    probe = pad.add_probe(gst.PadProbeType.BUFFER, record_timestamp)
    try:
        if sdp is not None:
            answered = Event()

            def on_message(_, kind, data):
                if json.loads(data.get_data())["type"] == "answer":
                    answered.set()

            websocket.connect("message", on_message)
            websocket.send_text(json.dumps({"type": "offer", "sdp": sdp}))
            wait_for(answered.is_set, "SDP accepted")
        before = len(timestamps)
        wait_for(lambda: len(timestamps) >= before + 5, "Silence stopped without media", timeout=2)
        assert timestamps[-1] > timestamps[before], "PCM timestamps stopped"
        wait_for(lambda: websocket.get_state() == Soup.WebsocketState.CLOSED,
                 "Peer without media retained the microphone slot", timeout=20)
    finally:
        pad.remove_probe(probe)


def negotiate(gst, session, source, port, payload):
    sender = gst.parse_launch(
        "audiotestsrc is-live=true ! audioconvert ! audioresample ! "
        "audio/x-raw,rate=48000,channels=2 ! opusenc ! "
        f"rtpopuspay pt={payload} ! "
        f"application/x-rtp,media=audio,encoding-name=OPUS,clock-rate=48000,payload={payload} ! "
        "webrtcbin name=peer"
    )
    peer = sender.get_by_name("peer")
    transceiver = peer.get_static_pad("sink_0").get_property("transceiver")
    transceiver.set_property("direction", GstWebRTC.WebRTCRTPTransceiverDirection.SENDONLY)
    websocket = connect(session, port)
    received = Event()
    decoded_pad = source.get_static_pad("src")

    def decoded(_, info):
        buffer = info.get_buffer()
        if buffer.get_size() and any(buffer.extract_dup(0, buffer.get_size())):
            received.set()
        return gst.PadProbeReturn.OK

    probe = decoded_pad.add_probe(gst.PadProbeType.BUFFER, decoded)
    errors = []
    answered = []
    remote_candidates = []

    def incoming(_, kind, data):
        try:
            message = json.loads(data.get_data())
            if message["type"] == "answer":
                result, sdp = GstSdp.SDPMessage.new_from_text(message["sdp"])
                assert result == GstSdp.SDPResult.OK
                assert "a=recvonly" in message["sdp"]
                answer = GstWebRTC.WebRTCSessionDescription.new(
                    GstWebRTC.WebRTCSDPType.ANSWER, sdp
                )
                invoke(gst, peer, "set-remote-description", answer)
                answered.append(True)
                for candidate in remote_candidates:
                    peer.emit("add-ice-candidate", candidate["sdpMLineIndex"], candidate["candidate"])
                remote_candidates.clear()
            elif message["type"] == "candidate":
                candidate = message["ice"]
                if answered:
                    peer.emit("add-ice-candidate", candidate["sdpMLineIndex"], candidate["candidate"])
                else:
                    remote_candidates.append(candidate)
        except Exception as error:
            errors.append(error)

    def local_candidate(_, index, candidate):
        def send():
            if websocket.get_state() == Soup.WebsocketState.OPEN:
                websocket.send_text(json.dumps({
                    "type": "candidate",
                    "ice": {"candidate": candidate, "sdpMLineIndex": index},
                }))
            return False
        GLib.idle_add(send)

    peer.connect("on-ice-candidate", local_candidate)
    try:
        assert sender.set_state(gst.State.PLAYING) != gst.StateChangeReturn.FAILURE
        offer_reply = invoke(gst, peer, "create-offer", None)
        offer = offer_reply.get_value("offer")
        offer_text = offer.sdp.as_text()
        if payload == 111:
            check_stalled_peer(gst, source, websocket, offer_text)
            websocket = connect(session, port)
        websocket.connect("message", incoming)
        for invalid_sdp in (
            offer_text.replace("a=sendonly", "a=recvonly"),
            offer_text.replace("OPUS/48000", "PCMU/48000"),
            offer_text + "\0",
        ):
            websocket.send_text(json.dumps({"type": "offer", "sdp": invalid_sdp}))
            wait_for(lambda: websocket.get_state() == Soup.WebsocketState.CLOSED,
                     "Unsupported SDP rejected")
            websocket = connect(session, port)
            websocket.connect("message", incoming)
        invoke(gst, peer, "set-local-description", offer)
        websocket.send_text(json.dumps({"type": "offer", "sdp": offer_text}))
        wait_for(lambda: errors or received.is_set(), "Decoded audio after SDP/ICE negotiation")
        assert not errors, errors
        assert answered

        second = connect(session, port)
        wait_for(lambda: second.get_state() == Soup.WebsocketState.CLOSED, "Extra client rejected")
        assert websocket.get_state() == Soup.WebsocketState.OPEN
        selector = source.get_by_name("input-selector")
        audio = source.get_by_name("audioresample").get_static_pad("src").get_peer()
        assert selector.get_property("active-pad") == audio
    finally:
        previous_peer = source.get_by_name("webrtcbin")
        websocket.close(1000, "done")
        release_pipeline(sender, gst)
        decoded_pad.remove_probe(probe)
        wait_for(lambda: source.get_by_name("webrtcbin") != previous_peer, "Receiver reset")
        silence = source.get_by_name("audiotestsrc").get_static_pad("src").get_peer()
        assert source.get_by_name("input-selector").get_property("active-pad") == silence


def main():
    gst = load_gstreamer_plugins([Path(sys.argv[1])])
    session = Soup.Session()
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    pipeline = gst.parse_launch(
        f"opksrc name=source ws-port={port} ! fakesink sync=false"
    )
    source = pipeline.get_by_name("source")
    try:
        for cycle in range(2):
            assert pipeline.set_state(gst.State.PLAYING) != gst.StateChangeReturn.FAILURE
            assert pipeline.get_state(5 * gst.SECOND)[1] == gst.State.PLAYING
            if cycle == 0:
                check_stalled_peer(gst, source, connect(session, port))
            for message in (
                "{",
                '{"type":"offer","sdp":"not SDP"}',
                '{"type":"candidate","ice":{"candidate":"x","sdpMLineIndex":-1}}',
            ):
                websocket = connect(session, port)
                websocket.send_text(message)
                wait_for(lambda: websocket.get_state() == Soup.WebsocketState.CLOSED,
                         "Malformed message rejected")
            negotiate(gst, session, source, port, 111)
            negotiate(gst, session, source, port, 109)
            active = connect(session, port)
            release_pipeline(pipeline, gst)
            wait_for(lambda: active.get_state() == Soup.WebsocketState.CLOSED,
                     "Active connection closed on shutdown")
            with socket.socket() as check:
                check.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                check.bind(("127.0.0.1", port))
    finally:
        session.abort()
        release_pipeline(pipeline, gst)

    with socket.socket() as occupied:
        occupied.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        occupied.bind(("127.0.0.1", port))
        occupied.listen()
        assert pipeline.set_state(gst.State.READY) == gst.StateChangeReturn.FAILURE
        release_pipeline(pipeline, gst)
    try:
        assert pipeline.set_state(gst.State.PLAYING) != gst.StateChangeReturn.FAILURE
        assert pipeline.get_state(5 * gst.SECOND)[1] == gst.State.PLAYING
        with socket.create_connection(("127.0.0.1", port), timeout=2):
            started = monotonic()
            release_pipeline(pipeline, gst)
            assert monotonic() - started < 5, "Incomplete handshake delayed shutdown"
    finally:
        release_pipeline(pipeline, gst)


if __name__ == "__main__":
    main()
