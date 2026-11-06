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
        if header[0] & 0x0F == 9:
            send_frame(stream, 10, payload)


def handshake(stream, port):
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


def main():
    gst = load_gstreamer_plugins([Path(path) for path in sys.argv[1:]])
    with ExitStack() as reservations:
        ports, _ = reserve_ports(reservations, 4)
        http, webrtc, ctrl, comm = ports
        pipeline = gst.parse_launch(
            "videotestsrc is-live=true ! "
            "video/x-raw,format=I420,width=160,height=120,framerate=5/1 ! "
            f"opkcomm name=publisher method=websocket ws-port={comm} ! "
            f"opksink http-port={http} ws-port={webrtc} ctrl-port={ctrl}"
        )
    publisher = pipeline.get_by_name("publisher")
    disconnect_endpoints = []
    disconnected = threading.Event()

    def read_endpoint_on_disconnect(category, level, filename, function, line, obj, message, data):
        if obj == publisher and message.get() == "opkcomm WebSocket client disconnected":
            disconnect_endpoints.append(publisher.get_property("endpoint"))
            disconnected.set()

    category = next(category for category in gst.debug_get_all_categories()
                    if category.get_name() == "default")
    previous_threshold = category.get_threshold()
    gst.debug_set_threshold_for_name("default", gst.DebugLevel.INFO)
    gst.debug_add_log_function(read_endpoint_on_disconnect, None)
    try:
        for port, replacement in ((webrtc, None), (ctrl, None), (comm, "/replacement"), (comm, None)):
            publisher.set_property("endpoint", "/ws")
            disconnect_endpoints.clear()
            disconnected.clear()
            assert pipeline.set_state(gst.State.PLAYING) != gst.StateChangeReturn.FAILURE
            assert pipeline.get_state(5 * gst.SECOND)[1] == gst.State.PLAYING
            with socket.create_connection(("127.0.0.1", port), timeout=3) as client:
                with client.makefile("rwb") as stream:
                    handshake(stream, port)
                    # Pong confirms the server has finished opening the connection.
                    send_frame(stream, 9, b"ready")
                    assert receive_until(stream, 10) == b"ready"
                    if replacement is None:
                        stopper = threading.Thread(
                            target=release_pipeline, args=(pipeline, gst), daemon=True
                        )
                    else:
                        stopper = threading.Thread(
                            target=publisher.set_property,
                            args=("endpoint", replacement), daemon=True
                        )
                    stopper.start()
                    close = receive_until(stream, 8)
                    assert close == b"\x03\xe9", close  # Going away (1001).
                    send_frame(stream, 8, close)
                    assert stream.read(1) == b""
                    stopper.join(3)
                    assert not stopper.is_alive(), "Writer shutdown or reconfiguration hung"
                    if port == comm:
                        assert disconnected.wait(3), "No disconnect callback"
                        assert disconnect_endpoints == [replacement or "/ws"]
                    if replacement is not None:
                        assert pipeline.get_state(0)[1] == gst.State.PLAYING
                        release_pipeline(pipeline, gst)
                    assert pipeline.get_state(0)[1] == gst.State.NULL
            check_released_ports(ports)
    finally:
        gst.debug_remove_log_function(read_endpoint_on_disconnect)
        gst.debug_set_threshold_for_name("default", previous_threshold)
        release_pipeline(pipeline, gst)


if __name__ == "__main__":
    main()
