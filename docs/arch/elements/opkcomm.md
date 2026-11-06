<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
SPDX-License-Identifier: Apache-2.0

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

# opkcomm

`opkcomm` passes buffers through and publishes their FrameResults using a file,
stdout, WebSocket or TCP writer. Set `method` and its corresponding destination
properties before startup; inspect enum names and defaults with
`gst-inspect-1.0 opkcomm`. `file-name=-` selects stdout for file output.

## Properties

**Live** applies while playing. A request can still require an explicit restart
when its destination overlaps the active listening address; inspect `property-status`.

| Property | Purpose | Default | Accepted values / Example | Changeable in |
| --- | --- | --- | --- | --- |
| `method` | Choose the FrameResults writer. | `file` (`1`) | `file` (`1`), `websocket` (`2`), `tcp` (`3`). | Live |
| `file-name` | Output file for `method=file`. | `-` (stdout) | File path; e.g. `/tmp/results.jsonl`. `-` selects stdout. | Live |
| `ws-port` | Listening port for `method=websocket`. | `8002` | Unsigned integer, 1–65535; e.g. `9002`. | Live |
| `endpoint` | WebSocket endpoint path for `method=websocket`. | `/ws` | Path; e.g. `/metadata`. | Live |
| `tcp-host` | Bind address for `method=tcp`. | `127.0.0.1` | Address; e.g. `0.0.0.0` to listen on all IPv4 interfaces. | Live |
| `tcp-port` | Listening port for `method=tcp`. | `7001` | Unsigned integer, 1–65535; e.g. `7002`. | Live |
| `property-status` | Latest per-property request, effective value, outcome and restart requirement. | `null` before any request | Read-only `GstStructure`; see the shared configuration guide. | Read-only |

A live request prepares a replacement asynchronously while the current writer
continues. At handoff it finishes the current record, transfers unstarted queued
jobs in order, and starts the replacement. Files retain append semantics. The
queue remains bounded; new frames arriving at a full queue retain the existing
drop policy. Changing an unused destination setting does not reopen the active
writer. Use `configure-properties` to change a method and its destination together.

TCP and FIFO backpressure retains the unfinished record at its original destination.
WebSocket records already submitted to the transport also belong to the old
destination. A ping/pong barrier confirms their completion before switching;
clients must answer standard WebSocket pings. The handoff waits at most five
seconds for completion. Timeout reports a failed request and resumes the old
queue if it is still valid. A late completion never performs a failed switch.

TCP and WebSocket queues wait for a client before consuming retained jobs.
Connections are not transferred to a different destination. Changing only a
WebSocket endpoint reuses the listener and closes existing clients after their
pending writes complete; clients reconnect to the new path. An overlapping
listener that cannot be opened beside the current one requires an explicit READY
restart. Its requested values remain pending with that reason.

Failure to prepare a replacement preserves the current writer and reports the
failed requested values separately from effective values. Failure to finish an
actual record posts an ERROR and stops affected processing. Stopping the element
explicitly discards queued work as before. No previous configurations are saved
and no historical writer is restored.

Applications must connect to the new destination. `opksink` caches browser metadata
connection settings at NULL → READY; changing `opkcomm` does not retarget already
connected browsers or that cached configuration.

See [Configure GStreamer elements](../../public/how-to/configure-elements.md)
for discovery, readback, validation and the explicit restart procedure.
