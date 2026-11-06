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

In the table, **Live** permits
writes in NULL, READY, PAUSED and PLAYING; **NULL/READY** and **NULL** restrict
writes to those states. Read-only entries cannot be set.

| Property | Purpose | Default | Accepted values / Example | Changeable in |
| --- | --- | --- | --- | --- |
| `method` | Choose the FrameResults writer. | `file` (`1`) | `file` (`1`), `websocket` (`2`), `tcp` (`3`). | Live |
| `file-name` | Output file for `method=file`. | `-` (stdout) | File path; e.g. `/tmp/results.jsonl`. `-` selects stdout. | Live |
| `ws-port` | Listening port for `method=websocket`. | `8002` | Unsigned integer, 1–65535; e.g. `9002`. | Live |
| `endpoint` | WebSocket endpoint path for `method=websocket`. | `/ws` | Path; e.g. `/metadata`. | Live |
| `tcp-host` | Bind address for `method=tcp`. | `127.0.0.1` | Address; e.g. `0.0.0.0` to listen on all IPv4 interfaces. | Live |
| `tcp-port` | Listening port for `method=tcp`. | `7001` | Unsigned integer, 1–65535; e.g. `7002`. | Live |

The existing setters also support live changes by synchronously closing and
reopening the writer. Configuration, processing and writer lifetime are
serialized. This is a disruptive operation: clients can disconnect and queued
records can be discarded. Setting several properties performs several reopen
operations; stop the pipeline first when changing a destination combination.
There is no queue redirection or record-boundary handoff yet.

If opening the writer fails, the element posts an ERROR and affected processing
returns an error instead of silently continuing without an output. The requested
properties remain readable; the caller must correct them and restart. OPK does
not restore the old writer or a previous configuration.

See [Configure GStreamer elements](../../public/how-to/configure-elements.md)
for discovery, readback, validation and the explicit restart procedure.
