#!/usr/bin/env python3
################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################

"""Configured ports, concurrent pipelines, and retry after a failed startup."""

from contextlib import ExitStack
import errno
from pathlib import Path
import socket
import sys
from urllib.request import urlopen

from pipeline_test_utils import load_gstreamer_plugins, release_pipeline


def make_pipeline(gst, ports):
    http, ws, ctrl = ports
    return gst.parse_launch(
        "videotestsrc is-live=true ! "
        "video/x-raw,format=I420,width=160,height=120,framerate=5/1 ! "
        f"opksink name=sink http-port={http} ws-port={ws} ctrl-port={ctrl}"
    )


def reserve_ports(reservations, count):
    sockets = [reservations.enter_context(socket.socket()) for _ in range(count)]
    for listener in sockets:
        listener.bind(("127.0.0.1", 0))
    return [listener.getsockname()[1] for listener in sockets], sockets


def check_endpoints(ports):
    http, ws, ctrl = ports
    with urlopen(f"http://127.0.0.1:{http}/opk-config.js", timeout=2) as response:
        config = response.read().decode()
    assert f'"wsPort":{ws}' in config
    assert f'"ctrlPort":{ctrl}' in config
    for port in (ws, ctrl):
        with socket.create_connection(("127.0.0.1", port), timeout=2):
            pass


def check_released_ports(ports):
    for port in ports:
        with socket.socket() as client:
            client.settimeout(2)
            assert client.connect_ex(("127.0.0.1", port)) == errno.ECONNREFUSED, port


def main() -> None:
    gst = load_gstreamer_plugins([Path(sys.argv[1])])
    pipelines = []
    try:
        # Hold six distinct ports until both pipelines have been constructed.
        with ExitStack() as reservations:
            ports, _ = reserve_ports(reservations, 6)
            port_sets = [ports[:3], ports[3:]]
            for pipeline_ports in port_sets:
                pipelines.append(make_pipeline(gst, pipeline_ports))

        for _ in range(2):
            for pipeline in pipelines:
                assert pipeline.set_state(gst.State.PLAYING) != gst.StateChangeReturn.FAILURE
                assert pipeline.get_state(5 * gst.SECOND)[1] == gst.State.PLAYING
            for pipeline_ports in port_sets:
                check_endpoints(pipeline_ports)
            for pipeline in pipelines:
                release_pipeline(pipeline, gst)
            for pipeline_ports in port_sets:
                check_released_ports(pipeline_ports)

        # An unrelated pipeline must remain usable when another cannot start.
        for pipeline in pipelines:
            assert pipeline.set_state(gst.State.PLAYING) != gst.StateChangeReturn.FAILURE

        # Keep the other pipeline running while this instance changes ports.
        for index, pipeline in enumerate(pipelines):
            assert pipelines[1 - index].get_state(0)[1] == gst.State.PLAYING
            with ExitStack() as reservations:
                new_ports, _ = reserve_ports(reservations, 3)
                release_pipeline(pipeline, gst)
                check_released_ports(port_sets[index])
                check_endpoints(port_sets[1 - index])
                sink = pipeline.get_by_name("sink")
                for name, port in zip(("http-port", "ws-port", "ctrl-port"), new_ports):
                    sink.set_property(name, port)
            assert pipeline.set_state(gst.State.PLAYING) != gst.StateChangeReturn.FAILURE
            assert pipeline.get_state(5 * gst.SECOND)[1] == gst.State.PLAYING
            check_endpoints(new_ports)
            port_sets[index] = new_ports

        # Failed HTTP startup must leave the other pipeline usable and allow retry.
        pipeline = pipelines[1]
        sink = pipeline.get_by_name("sink")
        for name, value in (("host", None), ("http-port", port_sets[0][0])):
            release_pipeline(pipeline, gst)
            original = sink.get_property(name)
            sink.set_property(name, value)
            assert pipeline.set_state(gst.State.READY) == gst.StateChangeReturn.FAILURE
            assert pipeline.get_bus().timed_pop_filtered(gst.SECOND, gst.MessageType.ERROR)
            assert pipelines[0].get_state(0)[1] == gst.State.PLAYING
            check_endpoints(port_sets[0])
            check_released_ports(port_sets[1])
            release_pipeline(pipeline, gst)
            sink.set_property(name, original)
            assert pipeline.set_state(gst.State.PLAYING) != gst.StateChangeReturn.FAILURE
            assert pipeline.get_state(5 * gst.SECOND)[1] == gst.State.PLAYING
            check_endpoints(port_sets[1])

        for conflict in range(3):
            with ExitStack() as reservations:
                blocked_ports, sockets = reserve_ports(reservations, 3)
                for index, listener in enumerate(sockets):
                    if index != conflict:
                        listener.close()
                blocked = make_pipeline(gst, blocked_ports)
                bus = blocked.get_bus()
                control_connections = []

                def on_startup_error(bus, message, ctrl_port):
                    if message.type == gst.MessageType.ERROR:
                        with socket.socket() as client:
                            client.settimeout(2)
                            control_connections.append(client.connect_ex(("127.0.0.1", ctrl_port)))
                    return gst.BusSyncReply.PASS

                if conflict == 0:
                    # Control commands must not be accepted before HTTP startup succeeds.
                    bus.set_sync_handler(on_startup_error, blocked_ports[2])
                try:
                    assert blocked.set_state(gst.State.READY) == gst.StateChangeReturn.FAILURE
                    assert bus.timed_pop_filtered(gst.SECOND, gst.MessageType.ERROR)
                    if conflict == 0:
                        assert control_connections == [errno.ECONNREFUSED], control_connections
                    # Rollback must release ports before the failed pipeline is destroyed.
                    check_released_ports([
                        port for index, port in enumerate(blocked_ports) if index != conflict
                    ])
                    for pipeline_ports in port_sets:
                        check_endpoints(pipeline_ports)
                    release_pipeline(blocked, gst)
                    sockets[conflict].close()
                    assert blocked.set_state(gst.State.PLAYING) != gst.StateChangeReturn.FAILURE
                    assert blocked.get_state(5 * gst.SECOND)[1] == gst.State.PLAYING
                    check_endpoints(blocked_ports)
                finally:
                    bus.set_sync_handler(None, None)
                    release_pipeline(blocked, gst)
    finally:
        for pipeline in pipelines:
            release_pipeline(pipeline, gst)


if __name__ == "__main__":
    main()
