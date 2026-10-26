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


"""Report microphone availability through the control WebSocket."""

import json
from pathlib import Path
import socket
import sys

from opksrc_signaling_test import connect, wait_for
from pipeline_test_utils import load_gstreamer_plugins, release_pipeline
from gi.repository import Soup


def main():
    gst = load_gstreamer_plugins([Path(path) for path in sys.argv[1:]])
    session = Soup.Session()
    reservations = [socket.socket() for _ in range(3)]
    for reservation in reservations:
        reservation.bind(("127.0.0.1", 0))
    http_port, ws_port, ctrl_port = [item.getsockname()[1] for item in reservations]
    pipeline = gst.parse_launch(
        f"videotestsrc is-live=true ! video/x-raw,format=BGRA ! "
        f"opksink name=sink http-port={http_port} ws-port={ws_port} ctrl-port={ctrl_port}"
    )
    for reservation in reservations:
        reservation.close()
    nested = gst.Bin.new("nested")
    pipeline.add(nested)
    microphone = gst.ElementFactory.make("opksrc", "arbitrary-name")
    try:
        assert pipeline.set_state(gst.State.READY) != gst.StateChangeReturn.FAILURE
        for present in (False, True, False):
            if present:
                nested.add(microphone)
            elif microphone.get_parent() is not None:
                nested.remove(microphone)
            messages = []
            websocket = connect(session, ctrl_port)
            websocket.connect("message", lambda _, kind, data: messages.append(
                json.loads(data.get_data())
            ))
            wait_for(lambda: messages, "Initial control status")
            assert messages[-1]["pipeline_state"]["microphone"] is present, messages
            websocket.close(1000, "done")
            wait_for(lambda: websocket.get_state() == Soup.WebsocketState.CLOSED,
                     "Control socket closed")
    finally:
        session.abort()
        release_pipeline(pipeline, gst)


if __name__ == "__main__":
    main()
