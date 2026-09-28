<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

# OpkComm / Plumber

## Overview

This system is used to record and compare pipeline metadata output from an Open Perception Kit pipeline.

Current status: `opkcomm` publishes serialized FrameResults packets in a JSON wrapper.
Plumber records that NDJSON unchanged in `save` mode and decodes
`frame_results_packet_b64` with the generated `open_perception_kit` Python SDK in `check` mode.
Plumber imports the installed package normally; the devcontainer installs the
descriptor-selected checked-in Python package in editable mode.

At a high level:

- **OpkComm** is a GStreamer element that publishes one **NDJSON** record per frame.
- **Plumber** is a Python-based test tool that:
  - can **save** the incoming NDJSON as Ground Truth
  - can **check** current FrameResults packet NDJSON against previously saved Ground Truth

OpkComm can publish the stream to a file/FIFO, a WebSocket endpoint, or a raw TCP socket. Plumber currently consumes the file/FIFO mode, while browser applications can consume the WebSocket mode directly.

OpkComm writes one JSON object per message. In file/FIFO and TCP modes each object is newline-delimited; in WebSocket mode each object is sent as one text message.

## Architecture

```text
Open Perception Kit pipeline
   |
   v
OpkComm (GStreamer element)
   |
   +--> file/FIFO mode
   |      |  NDJSON over FIFO
   |      v
   |   private per-run FIFO
   |      |
   |      v
   |   Plumber
   |      |
   |      +--> save mode  -> writes Ground Truth NDJSON
   |      |
   |      \--> check mode -> compares pipeline output to Ground Truth
   |
   +--> WebSocket mode
          |  JSON text messages
          v
       browser or another WebSocket client
   |
   \--> TCP mode
          |  NDJSON over TCP
          v
       TCP client
```

## Components

### OpkComm

OpkComm is responsible for publishing serialized FrameResults from the pipeline.

Relevant properties:

- `method`
  - publishing method
  - supported values: `file`, `websocket`, `tcp`
  - default: `file`
- `file-name`
  - output target path used when `method=file`
  - can be a normal file path, an existing FIFO path, or `-` for stdout
  - Plumber creates a private per-run FIFO unless `--fifo` selects another path
- `ws-port`
  - TCP port used when `method=websocket`
  - default: `8002`
- `endpoint`
  - WebSocket path used when `method=websocket`
  - must start with `/`
  - default: `/ws`
- `tcp-host`
  - bind host used when `method=tcp`
  - default: `127.0.0.1`
- `tcp-port`
  - listen port used when `method=tcp`
  - default: `7001`

Example GObject properties for Plumber/FIFO use:

- `method=file`
- `file-name=/tmp/opkcomm`

Example GObject properties for browser/WebSocket use:

- `method=websocket`
- `ws-port=8080`
- `endpoint=/ws`

Example GObject properties for raw TCP use:

- `method=tcp`
- `tcp-host=127.0.0.1`
- `tcp-port=7001`

### Plumber

Plumber is the regression / validation tool.

It starts a pipeline with `opk-menu`, reads the generated NDJSON stream from the FIFO, and either:

- stores it as Ground Truth
- or compares decoded FrameResults packets to an existing Ground Truth file

## Data Format

OpkComm writes serialized FrameResults packets inside JSON wrapper objects:

- file/FIFO mode writes **NDJSON**: one complete JSON object per line
- WebSocket mode sends one complete JSON object per text message
- TCP mode writes **NDJSON**: one complete JSON object per line
- the object shape is the same in all modes

Current FrameResults wrapper example:

```json
{"frame_counter":0,"frame_results_encoding":"perception-frame-results+base64","frame_results_packet_b64":"..."}
```

The `frame_results_packet_b64` value is a serialized FrameResults packet
encoded as base64. Plumber decodes it with the generated Python SDK and compares normalized
payload snapshots built from generated schema types such as:

Plumber uses the owning endpoint API from `open_perception_kit.packet`; the mutually
exclusive `open_perception_kit.guest` API is reserved for scripts attached to a live
C++ envelope. It requires the packet producer SDK name, semantic version, and
schema-set SHA-256 to exactly match the generated Open Perception Kit SDK used by
Plumber. Legacy packets without producer metadata and packets produced by a
different SDK revision are rejected before payload comparison.

```text
FrameContextT
BoxDetectionsT
ClassificationsT
PoseEstimationsT
SegmentationMasksT
ObjectEmbeddingsT
ObjectTracksT
TrackTracesT
```

`PerformanceOverlayT` is ignored by comparison because it is diagnostic runtime
text rather than stable model output.

External opaque payloads, when present in the packet, are decoded by the generated SDK as
external byte payloads. Plumber's current regression comparison intentionally normalizes
only known generated schema payloads.

## Plumber Usage

### Command line

```bash
plumber <pipeline> <mode> <file> [options]
```

Plumber uses `OPK_PROJECT_ROOT` as the working directory for `opk-menu` and as
the base of its default executable path. It defaults to `/work`, preserving the
container workflow. For a native checkout, set it to the absolute project path:

```bash
export OPK_PROJECT_ROOT="$(pwd)"
```

### Positional arguments

- `pipeline`
  - pipeline name passed to `opk-menu`
  - example: `onnx`

- `mode`
  - `save`
  - `check`

- `file`
  - in `save` mode: output Ground Truth file
  - in `check` mode: input Ground Truth file

### Options

- `--fifo`
  - path to FIFO used by OpkComm
  - default: a private per-run temporary FIFO

- `--opk-menu`
  - path to the `opk-menu` executable
  - default: `${OPK_PROJECT_ROOT:-/work}/tools/opk-menu`

- `--opk-menu-args`
  - extra arguments passed to `opk-menu`

- `--limit`
  - stop after N messages
  - `0` means unlimited

- `--verbose`
  - print extra debug information

- `--fail-fast`
  - stop at the first mismatch in `check` mode

## Examples

### Save Ground Truth

```bash
plumber onnx save gt.ndjson --fifo /tmp/opkcomm --limit 100
```

This will:

- start `opk-menu onnx`
- read NDJSON from `/tmp/opkcomm`
- save 100 messages into `gt.ndjson`

### Check Against Ground Truth

```bash
plumber onnx check gt.ndjson --fifo /tmp/opkcomm --fail-fast
```

This will:

- start `opk-menu onnx`
- read NDJSON from `/tmp/opkcomm`
- decode the incoming FrameResults packets with `open_perception_kit`
- compare the decoded payloads with `gt.ndjson`

## OpkComm Configuration Examples

For Plumber/FIFO workflows, configure OpkComm with a FIFO path:

```text
opkcomm method=file file-name=/tmp/opkcomm
```

For browser or other live WebSocket clients, configure OpkComm with a port and endpoint:

```text
opkcomm method=websocket ws-port=8080 endpoint=/ws
```

The resulting WebSocket URL is `ws://<host>:<ws-port><endpoint>`, for example `ws://127.0.0.1:8080/ws`.

For raw TCP clients, configure OpkComm with a bind host and port:

```text
opkcomm method=tcp tcp-host=127.0.0.1 tcp-port=7001
```

TCP clients receive newline-delimited JSON from the configured host and port.

## Typical Workflow

### 1. Record Ground Truth

Run Plumber in `save` mode on a known-good pipeline output.

### 2. Run Validation

Run Plumber in `check` mode and compare a new pipeline run against the saved Ground Truth.

This allows regression testing of pipeline output across code changes.

## Comparison Model

Plumber does not compare raw FlatBuffers bytes directly. It:

1. validates the NDJSON wrapper
2. base64-decodes `frame_results_packet_b64`
3. constructs a `FrameResults` object through the generated `open_perception_kit` SDK
4. validates the exact producer SDK name, version, and schema-set SHA-256
5. normalizes generated payload objects into payload snapshots
6. matches payload snapshots by generated payload type and stable `LayerInfo`
   fields
7. compares payload items with type-specific distance functions

External opaque payloads are outside the current comparison model because their byte
protocol is owned by the producer and not interpreted by the Open Perception Kit schema set.

The internal comparison vocabulary follows FrameResults terms:

- frame results
- payload
- payload type
- payload key
- payload snapshot
- item
- object index

Schema-specific names are kept when they are real generated concepts, such as
`BoxDetectionT`, `ClassificationT`, `PoseEstimationT`, `ObjectTrackT`, and
`TrackTraceT`.

## Notes

- FIFO output is used for communication between OpkComm and Plumber.
- WebSocket output is useful for browser frontends and live monitoring tools.
- TCP output is useful for clients that consume raw newline-delimited metadata without WebSocket framing.
- Ground Truth is stored as NDJSON.
- NDJSON is used by Plumber because it is simple, stream-friendly, and easy to process line by line.
