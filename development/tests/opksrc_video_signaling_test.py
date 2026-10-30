"""Receive VP8 and Opus on one peer and renegotiate independent capture tracks."""

import json
from pathlib import Path
import socket
import sys

from opksrc_signaling_test import connect, invoke, wait_for
from pipeline_test_utils import load_gstreamer_plugins, release_pipeline

import gi

gi.require_version("Soup", "3.0")
gi.require_version("GstWebRTC", "1.0")
gi.require_version("GstSdp", "1.0")

from gi.repository import GstSdp, GstWebRTC, Soup


def run(gst, source, port, video_linked=True):
    session = Soup.Session()
    websocket = connect(session, port)
    sender = gst.parse_launch(
        "videotestsrc is-live=true pattern=red ! video/x-raw,width=320,height=240,framerate=15/1 ! "
        "valve name=camera drop-mode=transform-to-gap ! vp8enc deadline=1 keyframe-max-dist=15 ! "
        "rtpvp8pay pt=97 ! application/x-rtp,media=video,encoding-name=VP8,clock-rate=90000,payload=97 ! "
        "webrtcbin name=peer bundle-policy=max-bundle"
    )
    peer = sender.get_by_name("peer")
    video = peer.get_static_pad("sink_0").get_property("transceiver")
    video.set_property("direction", GstWebRTC.WebRTCRTPTransceiverDirection.SENDONLY)
    answers = []
    errors = []
    candidates = []
    samples = {"audio": 0, "video": 0}

    def decoded(kind, info):
        buffer = info.get_buffer()
        data = buffer.extract_dup(0, min(buffer.get_size(), 4096))
        if any(data[2::4] if kind == "video" else data):
            samples[kind] += 1
        return gst.PadProbeReturn.OK

    pads = [source.get_static_pad("src"),
            source.get_static_pad("video") if video_linked else
            source.get_by_name("video-received").get_static_pad("src")]
    probes = [pads[0].add_probe(gst.PadProbeType.BUFFER, lambda _, info: decoded("audio", info)),
              pads[1].add_probe(gst.PadProbeType.BUFFER, lambda _, info: decoded("video", info))]

    def incoming(_, kind, data):
        try:
            message = json.loads(data.get_data())
            if message["type"] == "answer":
                result, sdp = GstSdp.SDPMessage.new_from_text(message["sdp"])
                assert result == GstSdp.SDPResult.OK
                answer = GstWebRTC.WebRTCSessionDescription.new(GstWebRTC.WebRTCSDPType.ANSWER, sdp)
                invoke(gst, peer, "set-remote-description", answer)
                answers.append(message["sdp"])
                for candidate in candidates:
                    peer.emit("add-ice-candidate", candidate["sdpMLineIndex"], candidate["candidate"])
                candidates.clear()
            elif answers:
                candidate = message["ice"]
                peer.emit("add-ice-candidate", candidate["sdpMLineIndex"], candidate["candidate"])
            else:
                candidates.append(message["ice"])
        except Exception as error:
            errors.append(error)

    websocket.connect("message", incoming)

    def local_candidate(_, index, candidate):
        from gi.repository import GLib

        def send():
            if websocket.get_state() == Soup.WebsocketState.OPEN:
                websocket.send_text(json.dumps({"type": "candidate", "ice": {
                    "candidate": candidate, "sdpMLineIndex": index}}))
            return False
        GLib.idle_add(send)

    peer.connect("on-ice-candidate", local_candidate)

    def offer():
        count = len(answers)
        reply = invoke(gst, peer, "create-offer", None)
        description = reply.get_value("offer")
        invoke(gst, peer, "set-local-description", description)
        websocket.send_text(json.dumps({"type": "offer", "sdp": description.sdp.as_text()}))
        wait_for(lambda: errors or len(answers) > count or websocket.get_state() == Soup.WebsocketState.CLOSED,
                 "Renegotiation answer")
        assert not errors, errors
        assert len(answers) > count, "Receiver rejected offer"

    try:
        assert sender.set_state(gst.State.PLAYING) != gst.StateChangeReturn.FAILURE
        invalid_reply = invoke(gst, peer, "create-offer", None)
        invalid = invalid_reply.get_value("offer").sdp.as_text().replace("VP8/90000", "H264/90000")
        websocket.send_text(json.dumps({"type": "offer", "sdp": invalid}))
        wait_for(lambda: websocket.get_state() == Soup.WebsocketState.CLOSED, "Unsupported video codec rejected")
        websocket = connect(session, port)
        websocket.connect("message", incoming)
        offer()
        wait_for(lambda: samples["video"] > 3, "Decoded camera-only VP8")
        audio_bin = gst.parse_bin_from_description(
            "audiotestsrc is-live=true ! valve name=microphone drop-mode=transform-to-gap ! "
            "audioconvert ! audioresample ! audio/x-raw,rate=48000,channels=2 ! opusenc ! "
            'rtpopuspay pt=111 ! capsfilter caps="application/x-rtp,media=audio,encoding-name=OPUS,clock-rate=48000,payload=111"', True)
        sender.add(audio_bin)
        audio_sink = peer.request_pad_simple("sink_%u")
        assert audio_bin.get_static_pad("src").link(audio_sink) == gst.PadLinkReturn.OK
        audio = audio_sink.get_property("transceiver")
        audio.set_property("direction", GstWebRTC.WebRTCRTPTransceiverDirection.SENDONLY)
        audio_bin.sync_state_with_parent()
        offer()
        wait_for(lambda: samples["audio"] > 3, "Decoded audio alongside video")
        replacements = []
        for transceiver, valve, selector_name, fallback_name, remaining in (
            (video, sender.get_by_name("camera"), "video-selector", "video-black", "audio"),
            (audio, audio_bin.get_by_name("microphone"), "input-selector", "audiotestsrc", "video"),
        ):
            before = samples[remaining]
            valve.set_property("drop", True)
            transceiver.set_property("direction", GstWebRTC.WebRTCRTPTransceiverDirection.INACTIVE)
            offer()
            selector = source.get_by_name(selector_name)
            fallback = source.get_by_name(fallback_name).get_static_pad("src").get_peer()
            wait_for(lambda: selector.get_property("active-pad") == fallback, "Disabled track fallback")
            wait_for(lambda: samples[remaining] > before + 3, "Other track interrupted")
            if remaining == "audio":
                replacement = gst.parse_bin_from_description(
                    "videotestsrc is-live=true pattern=red ! video/x-raw,width=320,height=240,framerate=15/1 ! "
                    "vp8enc deadline=1 keyframe-max-dist=15 ! rtpvp8pay pt=98 ! "
                    'capsfilter caps="application/x-rtp,media=video,encoding-name=VP8,clock-rate=90000,payload=98"', True)
            else:
                replacement = gst.parse_bin_from_description(
                    "audiotestsrc is-live=true ! audioconvert ! audioresample ! "
                    "audio/x-raw,rate=48000,channels=2 ! opusenc ! rtpopuspay pt=112 ! "
                    'capsfilter caps="application/x-rtp,media=audio,encoding-name=OPUS,clock-rate=48000,payload=112"', True)
            sender.add(replacement)
            replacement_sink = peer.request_pad_simple("sink_%u")
            assert replacement.get_static_pad("src").link(replacement_sink) == gst.PadLinkReturn.OK
            replacement_transceiver = replacement_sink.get_property("transceiver")
            replacement_transceiver.set_property("direction", GstWebRTC.WebRTCRTPTransceiverDirection.SENDONLY)
            replacements.append(replacement_transceiver)
            replacement.sync_state_with_parent()
            before = dict(samples)
            offer()
            wait_for(lambda: all(samples[kind] > before[kind] + 3 for kind in samples), "Capture resume")
        for transceiver in replacements:
            transceiver.set_property("direction", GstWebRTC.WebRTCRTPTransceiverDirection.INACTIVE)
        offer()
        for selector_name, fallback_name in (("video-selector", "video-black"), ("input-selector", "audiotestsrc")):
            fallback = source.get_by_name(fallback_name).get_static_pad("src").get_peer()
            assert source.get_by_name(selector_name).get_property("active-pad") == fallback
        assert websocket.get_state() == Soup.WebsocketState.OPEN
    finally:
        previous = source.get_by_name("webrtcbin")
        websocket.close(1000, "done")
        release_pipeline(sender, gst)
        for pad, probe in zip(pads, probes):
            pad.remove_probe(probe)
        wait_for(lambda: source.get_by_name("webrtcbin") != previous, "Receiver reset")
        black = source.get_by_name("video-black").get_static_pad("src").get_peer()
        assert source.get_by_name("video-selector").get_property("active-pad") == black
        session.abort()


if __name__ == "__main__":
    gst = load_gstreamer_plugins([Path(sys.argv[1])])
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    pipeline = gst.parse_launch(
        f"opksrc name=source ws-port={port} source.src ! queue ! fakesink sync=false "
        "source.video ! queue ! fakesink sync=false")
    try:
        assert pipeline.set_state(gst.State.PLAYING) != gst.StateChangeReturn.FAILURE
        for _ in range(2):
            run(gst, pipeline.get_by_name("source"), port)
        assert pipeline.get_bus().pop_filtered(gst.MessageType.ERROR) is None
    finally:
        release_pipeline(pipeline, gst)
    pipeline = gst.parse_launch(f"opksrc name=source ws-port={port} ! fakesink sync=false")
    try:
        assert pipeline.set_state(gst.State.PLAYING) != gst.StateChangeReturn.FAILURE
        run(gst, pipeline.get_by_name("source"), port, video_linked=False)
        assert pipeline.get_bus().pop_filtered(gst.MessageType.ERROR) is None
    finally:
        release_pipeline(pipeline, gst)
