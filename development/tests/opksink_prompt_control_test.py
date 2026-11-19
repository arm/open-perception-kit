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

"""Exercise actual prompt WebSocket ownership and reentrant host shutdown."""

from contextlib import ExitStack
import base64
import faulthandler
import json
from pathlib import Path
import subprocess
import sys
from time import monotonic, sleep

import gi
import flatbuffers
from open_perception_kit.fb.open_perception_kit.metadata.BoxPromptInputs import BoxPromptInputsT
from open_perception_kit.fb.open_perception_kit.metadata.BoxPromptPayload import BoxPromptPayloadT
from open_perception_kit.fb.open_perception_kit.metadata.PromptBox import PromptBoxT
from open_perception_kit.fb.open_perception_kit.metadata.RequestDestination import RequestDestinationT
from open_perception_kit.fb.open_perception_kit.metadata.RequestEnvelope import RequestEnvelopeT

from opksink_configured_ports_test import reserve_ports
from pipeline_test_utils import load_gstreamer_plugins

gi.require_version("Gst", "1.0")
gi.require_version("Soup", "3.0")
from gi.repository import GLib, GObject, Gst, Soup  # noqa: E402


REQUEST_ID = (1 << 63) + 7


def prompt_packet(revision=1):
    destination = RequestDestinationT(
        inferenceInstance="mobileSam", sourceName="pipelineVideoFrame"
    )
    request = RequestEnvelopeT(
        requestId=REQUEST_ID, revision=revision, destination=destination
    )
    box = PromptBoxT(left=0.1, top=0.1, right=0.9, bottom=0.9)
    payload = BoxPromptPayloadT(box=box)
    packet = BoxPromptInputsT(request=request, payload=payload)
    builder = flatbuffers.Builder(256)
    offset = packet.Pack(builder)
    builder.Finish(offset, file_identifier=b"BPRM")
    return base64.b64encode(builder.Output()).decode("ascii")


def wait_for(predicate, description, timeout=5):
    context = GLib.MainContext.default()
    deadline = monotonic() + timeout
    while not predicate():
        assert monotonic() < deadline, f"Timed out waiting for {description}"
        while context.pending():
            context.iteration(False)
        sleep(0.001)


def pump(duration=0.05):
    deadline = monotonic() + duration
    context = GLib.MainContext.default()
    while monotonic() < deadline:
        while context.pending():
            context.iteration(False)
        sleep(0.001)


class Client:
    def __init__(self, port):
        self.http = Soup.Session()
        self.messages = []
        self.sequence = 0
        completed = []
        request = Soup.Message.new("GET", f"ws://127.0.0.1:{port}/ws")

        def connected(owner, result):
            self.socket = self.http.websocket_connect_finish(result)
            self.socket.connect(
                "message",
                lambda owner, kind, data: self.messages.append(json.loads(data.get_data())),
            )
            completed.append(True)

        self.http.websocket_connect_async(
            request,
            None,
            None,
            0,
            None,
            connected,
        )
        wait_for(lambda: completed, "control connection")
        wait_for(
            lambda: any(item.get("type") == "prompt_capabilities" for item in self.messages),
            "prompt capabilities",
        )
        self.capabilities = next(
            item for item in self.messages if item.get("type") == "prompt_capabilities"
        )

    def command(self, operation, **fields):
        self.sequence += 1
        command_id = f"test-{self.sequence}"
        self.socket.send_text(
            json.dumps(
                {
                    "type": "prompt_command",
                    "version": 1,
                    "command_id": command_id,
                    "target": "prompts",
                    "command": operation,
                    **fields,
                }
            )
        )
        wait_for(
            lambda: any(item.get("command_id") == command_id for item in self.messages),
            f"{operation} reply",
        )
        replies = [item for item in self.messages if item.get("command_id") == command_id]
        assert len(replies) == 1, replies
        return replies[0]

    def submit(self, revision=1):
        return self.command("submit", payload_b64=prompt_packet(revision))

    def close(self):
        if self.socket.get_state() == Soup.WebsocketState.OPEN:
            self.socket.close(Soup.WebsocketCloseCode.NORMAL, "done")
        wait_for(
            lambda: self.socket.get_state() == Soup.WebsocketState.CLOSED,
            "control disconnect",
        )
        self.http.abort()


class Fixture:
    def __init__(self):
        with ExitStack() as reservations:
            ports, _ = reserve_ports(reservations, 3)
            self.http, self.rtc, self.ctrl = ports
            self.pipeline = Gst.parse_launch(
                "videotestsrc is-live=true ! "
                "video/x-raw,format=BGRA,width=8,height=8 ! "
                "opkprompt name=prompts destination-element=mobileSam "
                "inference-instance=mobileSam ! "
                "opkinfer name=mobileSam active=false ! "
                f"opksink name=sink host=127.0.0.1 http-port={self.http} "
                f"ws-port={self.rtc} ctrl-port={self.ctrl}"
            )
        self.bus = self.pipeline.get_bus()
        self.clients = []

    def start(self):
        # READY starts the control servers without loading a model or running inference.
        assert self.pipeline.set_state(Gst.State.READY) != Gst.StateChangeReturn.FAILURE
        assert self.pipeline.get_state(5 * Gst.SECOND)[1] == Gst.State.READY

    def connect(self):
        client = Client(self.ctrl)
        self.clients.append(client)
        return client

    def finish(self):
        self.bus.set_sync_handler(None, None)
        for client in self.clients:
            client.close()
        self.pipeline.set_state(Gst.State.NULL)


def assert_accepted(reply, accepted=True, revision=1):
    assert reply["type"] == "prompt_ack", reply
    assert reply["accepted"] is accepted, reply
    if accepted:
        assert reply["request_id"] == str(REQUEST_ID), reply
        assert reply["revision"] == str(revision), reply


def acquire_after_disconnect(client):
    deadline = monotonic() + 5
    while True:
        reply = client.submit()
        if reply.get("accepted"):
            return reply
        assert monotonic() < deadline, reply
        pump()


def basic_control():
    fixture = Fixture()
    try:
        observed = []

        def host_sync(bus, message, data):
            observed.append(message.type)
            return Gst.BusSyncReply.PASS

        fixture.bus.set_sync_handler(host_sync, None)
        fixture.start()
        first = fixture.connect()
        assert any("pipeline_state" in item and "models" in item for item in first.messages)
        target = next(item for item in first.capabilities["targets"] if item["target"] == "prompts")
        assert target["drawing_supported"], target
        assert_accepted(first.submit())
        second = fixture.connect()
        assert first.capabilities["session_id"] != second.capabilities["session_id"]
        assert_accepted(second.submit(), False)
        assert_accepted(first.submit(2), revision=2)
        assert_accepted(first.submit(1), False)

        invalid = first.command("submit", payload_b64="!!!!")
        assert invalid["type"] == "prompt_error", invalid
        invalid = first.command("submit", payload_b64="AAAA")
        assert_accepted(invalid, False)
        assert invalid["request_id"] == "0", invalid
        invalid = first.command("cancel", request_id=str(1 << 64), revision="2")
        assert invalid["type"] == "prompt_error", invalid

        cancel = first.command("cancel", request_id=str(REQUEST_ID), revision="2")
        assert_accepted(cancel, revision=2)
        assert_accepted(second.submit(), False)
        first.close()
        assert_accepted(acquire_after_disconnect(second))

        # Existing malformed-input handling and legacy model controls still work.
        class Model(Gst.Bin):
            active = GObject.Property(type=bool, default=False)

        model = Model(name="legacy-model")
        fixture.pipeline.add(model)
        second.socket.send_text("{")
        second.socket.send_text(json.dumps({"type": "model_toggle", "name": "legacy-model"}))
        wait_for(lambda: model.props.active, "legacy control after malformed JSON")

        inference = fixture.pipeline.get_by_name("mobileSam")

        def failure(session):
            structure = Gst.Structure.new_empty("opk-prompt-failure-v1")
            for name, value in {
                "session-id": session,
                "boundary": "opkinfer",
                "reason": "integration test",
                "request-id": GObject.Value(GObject.TYPE_UINT64, REQUEST_ID),
                "revision": GObject.Value(GObject.TYPE_UINT64, 1),
            }.items():
                structure.set_value(name, value)
            assert inference.post_message(Gst.Message.new_element(inference, structure))

        failure(first.capabilities["session_id"])
        pump()
        assert not any(item.get("type") == "prompt_failure" for item in second.messages)
        failure(second.capabilities["session_id"])
        wait_for(
            lambda: any(item.get("type") == "prompt_failure" for item in second.messages),
            "owned invocation failure",
        )
        assert Gst.MessageType.ELEMENT in observed
        assert fixture.bus.pop_filtered(Gst.MessageType.ELEMENT) is not None
        assert fixture.pipeline.post_message(Gst.Message.new_eos(fixture.pipeline))
        assert fixture.bus.pop_filtered(Gst.MessageType.EOS) is not None
        assert Gst.MessageType.EOS in observed
        second.close()
    finally:
        fixture.finish()


def synchronous_shutdown(operation):
    fixture = Fixture()
    completed = []
    try:
        def host_sync(bus, message, data):
            structure = message.get_structure()
            if (
                structure
                and structure.get_name() == "opk-prompt-ack-v1"
                and structure.get_value("command") == operation
            ):
                completed.append(fixture.pipeline.set_state(Gst.State.NULL))
            return Gst.BusSyncReply.PASS

        fixture.bus.set_sync_handler(host_sync, None)
        fixture.start()
        client = fixture.connect()
        if operation == "end-session":
            assert_accepted(client.submit())
            client.close()
        else:
            client.socket.send_text(json.dumps({
                "type": "prompt_command", "version": 1, "command_id": "stop-submit",
                "target": "prompts", "command": "submit", "payload_b64": prompt_packet(),
            }))
        wait_for(lambda: completed, f"host synchronous {operation} shutdown")
        assert completed[0] != Gst.StateChangeReturn.FAILURE
        assert fixture.pipeline.get_state(0)[1] == Gst.State.NULL
        fixture.bus.set_sync_handler(None, None)
        fixture.start()
        replacement = fixture.connect()
        assert replacement.capabilities["session_id"] != client.capabilities["session_id"]
        assert_accepted(acquire_after_disconnect(replacement))
        replacement.close()
    finally:
        fixture.finish()


def notification_backlog():
    fixture = Fixture()
    try:
        fixture.start()
        client = fixture.connect()
        assert_accepted(client.submit())
        ended = []

        def host_sync(bus, message, data):
            structure = message.get_structure()
            if (
                structure
                and structure.get_name() == "opk-prompt-ack-v1"
                and structure.get_value("command") == "end-session"
                and structure.get_value("accepted")
            ):
                ended.append(True)
            return Gst.BusSyncReply.PASS

        fixture.bus.set_sync_handler(host_sync, None)
        inference = fixture.pipeline.get_by_name("mobileSam")
        structure = Gst.Structure.new_empty("opk-prompt-failure-v1")
        for name, value in {
            "session-id": client.capabilities["session_id"],
            "boundary": "opkinfer",
            # A bounded reason whose JSON escaping fills the socket promptly.
            "reason": "\x01" * 1024,
            "request-id": GObject.Value(GObject.TYPE_UINT64, REQUEST_ID),
            "revision": GObject.Value(GObject.TYPE_UINT64, 1),
        }.items():
            structure.set_value(name, value)
        # Do not iterate GLib here: the Soup client deliberately stops reading.
        # Bound the flood even if a regression removes notification/backlog limits.
        for _ in range(20000):
            assert inference.post_message(Gst.Message.new_element(inference, structure.copy()))
            if ended:
                break
        wait_for(lambda: ended, "slow-client session cleanup")
        wait_for(
            lambda: client.socket.get_state() == Soup.WebsocketState.CLOSED,
            "slow-client control close",
        )
        replacement = fixture.connect()
        assert_accepted(acquire_after_disconnect(replacement))
        replacement.close()
    finally:
        fixture.finish()


def main():
    if len(sys.argv) > 2 and sys.argv[1] == "--scenario":
        faulthandler.dump_traceback_later(15, exit=True)
        load_gstreamer_plugins([Path(path) for path in sys.argv[3:]])
        if sys.argv[2] == "basic":
            basic_control()
        elif sys.argv[2] == "backlog":
            notification_backlog()
        else:
            synchronous_shutdown(sys.argv[2])
        faulthandler.cancel_dump_traceback_later()
        return
    # Isolate shutdown regressions so a deadlock/abort gives a bounded failure.
    for scenario in ("basic", "submit", "end-session", "backlog"):
        result = subprocess.run(
            [sys.executable, __file__, "--scenario", scenario, *sys.argv[1:]],
            capture_output=True,
            text=True,
            timeout=20,
            check=False,
        )
        assert result.returncode == 0, (
            f"{scenario} failed ({result.returncode})\n{result.stdout}\n{result.stderr}"
        )
        print(f"PASS: prompt control {scenario}")


if __name__ == "__main__":
    main()
