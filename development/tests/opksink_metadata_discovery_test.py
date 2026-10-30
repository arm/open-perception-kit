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

"""Metadata publisher discovery, overrides, and startup resolution lifecycle."""

from contextlib import ExitStack
import errno
import json
from pathlib import Path
import socket
import sys
import unittest
from urllib.request import urlopen

from opksink_configured_ports_test import check_released_ports, reserve_ports
from pipeline_test_utils import load_gstreamer_plugins, release_pipeline


class MetadataDiscoveryTest(unittest.TestCase):
    gst = None

    def setUp(self):
        self.reservations = ExitStack()
        self.addCleanup(self.reservations.close)
        self.listeners = {}

    def make_sink(self):
        ports, listeners = reserve_ports(self.reservations, 3)
        sink = self.gst.ElementFactory.make("opksink", None)
        self.assertIsNotNone(sink)
        for name, port in zip(("http-port", "ws-port", "ctrl-port"), ports):
            sink.set_property(name, port)
        self.listeners[sink] = listeners
        return sink, ports

    def make_pipeline(self):
        pipeline = self.gst.Pipeline.new(None)
        self.addCleanup(release_pipeline, pipeline, self.gst)
        sink, ports = self.make_sink()
        pipeline.add(sink)
        return pipeline, sink, ports

    def add_publisher(self, parent, port=9102, endpoint="/ws", method="websocket"):
        publisher = self.gst.ElementFactory.make("opkcomm", None)
        self.assertIsNotNone(publisher)
        publisher.set_property("method", method)
        publisher.set_property("ws-port", port)
        publisher.set_property("endpoint", endpoint)
        parent.add(publisher)
        return publisher

    def release_reservations(self, sink):
        for listener in self.listeners[sink]:
            listener.close()

    def start(self, pipeline, sink):
        self.release_reservations(sink)
        self.assertNotEqual(
            pipeline.set_state(self.gst.State.READY),
            self.gst.StateChangeReturn.FAILURE,
        )
        self.assertEqual(pipeline.get_state(5 * self.gst.SECOND)[1], self.gst.State.READY)

    def assert_metadata(self, ports, port, endpoint):
        with urlopen(f"http://127.0.0.1:{ports[0]}/opk-config.js", timeout=2) as response:
            script = response.read().decode()
        prefix = "window.OPK_CONFIG = "
        self.assertTrue(script.startswith(prefix), script)
        config = json.loads(script[len(prefix):].removesuffix(";"))
        self.assertEqual(config.get("metadataPort"), port, config)
        self.assertEqual(config.get("metadataEndpoint"), endpoint, config)

    def set_metadata_port(self, sink, port):
        self.assertIsNotNone(sink.find_property("metadata-port"))
        sink.set_property("metadata-port", port)

    def test_default_without_publisher(self):
        pipeline, sink, ports = self.make_pipeline()
        self.start(pipeline, sink)
        self.assert_metadata(ports, 8002, "/ws")
        self.assertEqual(sink.get_property("metadata-port"), 0)

    def test_single_websocket_publisher(self):
        pipeline, sink, ports = self.make_pipeline()
        self.add_publisher(pipeline, port=9102, endpoint="/perception/results")
        self.start(pipeline, sink)
        self.assert_metadata(ports, 9102, "/perception/results")
        self.assertEqual(sink.get_property("metadata-port"), 0)

    def test_nested_sibling_bins_share_containing_pipeline(self):
        pipeline, sink, ports = self.make_pipeline()
        pipeline.remove(sink)
        sink_parent = self.gst.Bin.new(None)
        sink_child = self.gst.Bin.new(None)
        sink_child.add(sink)
        sink_parent.add(sink_child)
        pipeline.add(sink_parent)
        publisher_parent = self.gst.Bin.new(None)
        publisher_child = self.gst.Bin.new(None)
        publisher_parent.add(publisher_child)
        pipeline.add(publisher_parent)
        self.add_publisher(publisher_child, port=9222, endpoint="/nested/metadata")
        self.start(pipeline, sink)
        self.assert_metadata(ports, 9222, "/nested/metadata")

    def test_file_and_tcp_publishers_are_ignored(self):
        pipeline, sink, ports = self.make_pipeline()
        self.add_publisher(pipeline, port=9111, endpoint="/file", method="file")
        self.add_publisher(pipeline, port=9222, endpoint="/tcp", method="tcp")
        self.start(pipeline, sink)
        self.assert_metadata(ports, 8002, "/ws")
        release_pipeline(pipeline, self.gst)
        self.add_publisher(pipeline, port=9333, endpoint="/websocket")
        self.start(pipeline, sink)
        self.assert_metadata(ports, 9333, "/websocket")

    def test_explicit_port_bypasses_multiple_publishers(self):
        pipeline, sink, ports = self.make_pipeline()
        self.add_publisher(pipeline, port=9102, endpoint="/first")
        self.add_publisher(pipeline, port=9222, endpoint="/second")
        # 8002 is an explicit override even though it is also the fallback port.
        for port in (8002, 1, 65535):
            with self.subTest(port=port):
                self.set_metadata_port(sink, port)
                self.start(pipeline, sink)
                self.assert_metadata(ports, port, "/ws")
                self.assertEqual(sink.get_property("metadata-port"), port)
                release_pipeline(pipeline, self.gst)

    def test_ambiguity_fails_before_servers_start_and_allows_explicit_retry(self):
        pipeline, sink, ports = self.make_pipeline()
        self.add_publisher(pipeline, port=9102, endpoint="/first")
        self.add_publisher(pipeline, port=9222, endpoint="/second")
        self.release_reservations(sink)
        connections_at_error = []
        bus = pipeline.get_bus()

        def on_error(_bus, message, _data):
            if message.type == self.gst.MessageType.ERROR:
                results = []
                for port in ports:
                    with socket.socket() as client:
                        client.settimeout(2)
                        results.append(client.connect_ex(("127.0.0.1", port)))
                connections_at_error.append(results)
            return self.gst.BusSyncReply.PASS

        bus.set_sync_handler(on_error, None)
        self.addCleanup(bus.set_sync_handler, None, None)
        self.assertEqual(
            pipeline.set_state(self.gst.State.READY), self.gst.StateChangeReturn.FAILURE
        )
        message = bus.timed_pop_filtered(self.gst.SECOND, self.gst.MessageType.ERROR)
        self.assertIsNotNone(message)
        error, debug = message.parse_error()
        detail = f"{error.message} {debug}"
        self.assertIn("metadata-port", detail)
        self.assertRegex(detail.lower(), "ambig|multiple|more than one")
        self.assertEqual(connections_at_error, [[errno.ECONNREFUSED] * 3])
        check_released_ports(ports)

        release_pipeline(pipeline, self.gst)
        self.set_metadata_port(sink, 8002)
        self.start(pipeline, sink)
        self.assert_metadata(ports, 8002, "/ws")

    def test_auto_explicit_auto_refreshes_port_and_endpoint(self):
        pipeline, sink, ports = self.make_pipeline()
        publisher = self.add_publisher(pipeline, port=9102, endpoint="/first")
        self.start(pipeline, sink)
        self.assert_metadata(ports, 9102, "/first")
        self.assertEqual(sink.get_property("metadata-port"), 0)

        release_pipeline(pipeline, self.gst)
        self.set_metadata_port(sink, 8002)
        publisher.set_property("ws-port", 9222)
        publisher.set_property("endpoint", "/second")
        self.start(pipeline, sink)
        self.assert_metadata(ports, 8002, "/ws")
        self.assertEqual(sink.get_property("metadata-port"), 8002)

        release_pipeline(pipeline, self.gst)
        self.set_metadata_port(sink, 0)
        self.start(pipeline, sink)
        self.assert_metadata(ports, 9222, "/second")
        self.assertEqual(sink.get_property("metadata-port"), 0)

        release_pipeline(pipeline, self.gst)
        publisher.set_property("ws-port", 9333)
        publisher.set_property("endpoint", "/third?channel=results&mode=live")
        self.start(pipeline, sink)
        self.assert_metadata(ports, 9333, "/third?channel=results&mode=live")

    def test_removed_publisher_restores_fallback_on_restart(self):
        pipeline, sink, ports = self.make_pipeline()
        publisher = self.add_publisher(pipeline, port=9102, endpoint="/first")
        self.start(pipeline, sink)
        self.assert_metadata(ports, 9102, "/first")
        release_pipeline(pipeline, self.gst)
        pipeline.remove(publisher)
        self.start(pipeline, sink)
        self.assert_metadata(ports, 8002, "/ws")

    def test_failed_http_startup_refreshes_discovery_on_retry(self):
        pipeline, sink, ports = self.make_pipeline()
        publisher = self.add_publisher(pipeline, port=9102, endpoint="/before-failure")
        # Keep the HTTP reservation bound so startup fails after resolution.
        for listener in self.listeners[sink][1:]:
            listener.close()
        self.assertEqual(
            pipeline.set_state(self.gst.State.READY), self.gst.StateChangeReturn.FAILURE
        )
        self.assertIsNotNone(
            pipeline.get_bus().timed_pop_filtered(self.gst.SECOND, self.gst.MessageType.ERROR)
        )
        check_released_ports(ports[1:])
        release_pipeline(pipeline, self.gst)
        publisher.set_property("ws-port", 9222)
        publisher.set_property("endpoint", "/after-failure")
        self.start(pipeline, sink)
        self.assert_metadata(ports, 9222, "/after-failure")
        self.assertEqual(sink.get_property("metadata-port"), 0)

    def test_discovery_is_isolated_between_running_pipelines(self):
        first, first_sink, first_ports = self.make_pipeline()
        self.add_publisher(first, port=9102, endpoint="/first")
        second, second_sink, second_ports = self.make_pipeline()
        second_publisher = self.add_publisher(second, port=9222, endpoint="/second")
        self.start(first, first_sink)
        self.start(second, second_sink)
        self.assert_metadata(first_ports, 9102, "/first")
        self.assert_metadata(second_ports, 9222, "/second")

        release_pipeline(second, self.gst)
        second_publisher.set_property("method", "file")
        self.start(second, second_sink)
        self.assert_metadata(first_ports, 9102, "/first")
        self.assert_metadata(second_ports, 8002, "/ws")

        release_pipeline(second, self.gst)
        second_publisher.set_property("method", "websocket")
        self.add_publisher(second, port=9333, endpoint="/ambiguous")
        self.assertEqual(
            second.set_state(self.gst.State.READY), self.gst.StateChangeReturn.FAILURE
        )
        self.assertIsNotNone(
            second.get_bus().timed_pop_filtered(self.gst.SECOND, self.gst.MessageType.ERROR)
        )
        self.assertEqual(first.get_state(0)[1], self.gst.State.READY)
        self.assert_metadata(first_ports, 9102, "/first")

    def test_unparented_sink_uses_fallback(self):
        sink, ports = self.make_sink()
        self.addCleanup(release_pipeline, sink, self.gst)
        self.start(sink, sink)
        self.assert_metadata(ports, 8002, "/ws")


if __name__ == "__main__":
    MetadataDiscoveryTest.gst = load_gstreamer_plugins([Path(path) for path in sys.argv[1:3]])
    unittest.main(argv=[sys.argv[0], *sys.argv[3:]], verbosity=2)
