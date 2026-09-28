#!/usr/bin/env python3
################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################

"""Graceful WebSocket close and port release for each server."""

from contextlib import ExitStack
import os
from pathlib import Path
import socket
import sys
import threading

from opksink_configured_ports_test import check_released_ports, reserve_ports
from pipeline_test_utils import load_gstreamer_plugins, release_pipeline


def send_frame(stream, opcode, payload):
    mask = os.urandom(4)
    masked = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
    stream.write(bytes([0x80 | opcode, 0x80 | len(payload)]) + mask + masked)
    stream.flush()


def receive_until(stream, opcode):
    while True:
        header = stream.read(2)
        assert len(header) == 2, "TCP closed without the expected WebSocket frame"
        length = header[1] & 0x7F
        if length in (126, 127):
            length = int.from_bytes(stream.read(2 if length == 126 else 8), "big")
        payload = stream.read(length)
        if header[0] & 0x0F == opcode:
            return payload


def main():
    gst = load_gstreamer_plugins([Path(path) for path in sys.argv[1:]])
    with ExitStack() as reservations:
        ports, _ = reserve_ports(reservations, 4)
        http, webrtc, ctrl, comm = ports
        pipeline = gst.parse_launch(
            "videotestsrc is-live=true ! "
            "video/x-raw,format=I420,width=160,height=120,framerate=5/1 ! "
            f"opkcomm method=websocket ws-port={comm} ! "
            f"opksink http-port={http} ws-port={webrtc} ctrl-port={ctrl}"
        )
    try:
        for port in (webrtc, ctrl, comm):
            assert pipeline.set_state(gst.State.PLAYING) != gst.StateChangeReturn.FAILURE
            assert pipeline.get_state(5 * gst.SECOND)[1] == gst.State.PLAYING
            with socket.create_connection(("127.0.0.1", port), timeout=3) as client:
                with client.makefile("rwb") as stream:
                    stream.write((
                        f"GET /ws HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n"
                        "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                        "Sec-WebSocket-Version: 13\r\n\r\n"
                    ).encode())
                    stream.flush()
                    assert stream.readline().startswith(b"HTTP/1.1 101 ")
                    while stream.readline().strip():
                        pass
                    # Pong confirms the server has finished opening the connection.
                    send_frame(stream, 9, b"ready")
                    assert receive_until(stream, 10) == b"ready"
                    stopper = threading.Thread(
                        target=release_pipeline, args=(pipeline, gst), daemon=True
                    )
                    stopper.start()
                    close = receive_until(stream, 8)
                    assert close == b"\x03\xe9", close  # Going away (1001).
                    send_frame(stream, 8, close)
                    assert stream.read(1) == b""
                    stopper.join(3)
                    assert not stopper.is_alive(), "Pipeline shutdown hung"
                    assert pipeline.get_state(0)[1] == gst.State.NULL
            check_released_ports(ports)
    finally:
        release_pipeline(pipeline, gst)


if __name__ == "__main__":
    main()
