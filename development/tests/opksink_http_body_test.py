#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Bodyless HTTP routes reject body framing before reading a body."""

from contextlib import ExitStack
from pathlib import Path
import socket
import sys

from pipeline_test_utils import load_gstreamer_plugins, release_pipeline


def request(port: int, target: str, headers: tuple[str, ...] = ()) -> bytes:
    with socket.create_connection(("127.0.0.1", port), timeout=2) as client:
        request_headers = "".join(f"{header}\r\n" for header in headers)
        client.sendall(
            f"GET {target} HTTP/1.1\r\nHost: 127.0.0.1\r\n"
            f"{request_headers}Connection: close\r\n\r\n".encode()
        )
        with client.makefile("rb") as response:
            return response.readline(8192)


def main() -> None:
    gst = load_gstreamer_plugins([Path(sys.argv[1])])
    with ExitStack() as reservations:
        sockets = [reservations.enter_context(socket.socket()) for _ in range(3)]
        for listener in sockets:
            listener.bind(("127.0.0.1", 0))
        ports = [listener.getsockname()[1] for listener in sockets]

        pipeline = gst.parse_launch(
            "videotestsrc is-live=true ! "
            "video/x-raw,format=I420,width=160,height=120,framerate=5/1 ! "
            f"opksink host=127.0.0.1 http-port={ports[0]} "
            f"ws-port={ports[1]} ctrl-port={ports[2]}"
        )
    try:
        assert pipeline.set_state(gst.State.PLAYING) != gst.StateChangeReturn.FAILURE
        assert pipeline.get_state(5 * gst.SECOND)[1] == gst.State.PLAYING

        assert request(ports[0], "/opk-config.js").startswith(b"HTTP/1.1 200 ")
        assert request(ports[0], "/").startswith(b"HTTP/1.1 200 ")

        for target in ("/opk-config.js", "/"):
            for header in ("Content-Length: 104857600", "Transfer-Encoding: chunked"):
                # No body bytes are sent: a prompt response proves rejection happened
                # before cpp-httplib tried to read or buffer the advertised body.
                response = request(ports[0], target, (header,))
                assert response.startswith(b"HTTP/1.1 400 "), response
                assert request(ports[0], target).startswith(b"HTTP/1.1 200 ")

        assert request(ports[0], "/opk-config.js", ("Content-Length: 0",)).startswith(
            b"HTTP/1.1 200 "
        )
    finally:
        release_pipeline(pipeline, gst)


if __name__ == "__main__":
    main()
