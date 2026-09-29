---
sidebar_position: 4
sidebar_label: opksink
---
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


# opksink

`opksink` is a `GstBin` that packages media encoding, WebRTC delivery, HTTP
serving, and control channels into one GStreamer element. It is primarily used to
make containerized or headless pipelines visible and controllable from a browser.

## Element Contract

- Base class: `GstBin`
- Video sink pad: `videosink`, always present, accepting
  `video/x-raw, format={BGRA,RGB,I420,NV12,YUY2}`
- Audio sink pad: `audiosink`, request pad
- Output: WebRTC transport rather than a normal downstream pad
- Video encoding: VP8
- Audio encoding: Opus, with silence fallback when no real audio is connected

The element internally builds and owns the media sub-pipeline needed for browser
streaming.

## Media Paths

The video path converts raw video, queues it, encodes VP8, synchronizes timing,
and feeds a tee. One branch goes to WebRTC; another drain branch prevents stalls
when no browser client is connected.

Experimental QoS feedback is available through `qos-enabled=true` and is disabled
by default. When enabled, the drain `fakesink` uses its normal clocked QoS behavior
to provide feedback without a WebRTC client. Set `qos-enabled=true` on the active
`opkinfer` to enable its experimental inference-skipping policy.

The audio path always has a silence source available. If the `audiosink` request
pad is used, an input selector switches from silence to real audio. A drain branch
serves the same non-blocking role as the video path.

## WebRTC, HTTP, And Control

`opksink` embeds several non-GStreamer services:

- a WebRTC WebSocket server for signaling
- a control WebSocket for runtime commands and status reporters
- an HTTP server for static browser content
- a model registry exposed through the control channel

Control messages with malformed JSON, an invalid `type`, or an invalid
`model_toggle` name are dropped with a debug log. The connection stays open and
subsequent commands remain usable. Invalid WebRTC message fields remove only the
affected session.
Browser diagnostics insert message text as DOM text, preserving HTML-like input literally.

`opkinfer` emits `opk-model-register` events, and `opksink` records the model
state and declared required/provided content types for browser-side visibility.
It also handles unregister events if an upstream component emits them.

The `/api/model-info` endpoint searches for model and OpChain descriptors under
`${OPK_PROJECT_ROOT:-/work}/config`. Explicitly setting `OPK_PROJECT_ROOT`
therefore keeps the browser's model information lookup aligned with pipelines
launched from a host checkout. Working-directory-relative locations remain as
fallbacks.

The browser receives serialized FrameResults records on the metadata WebSocket.
Its committed `opk-web.js` bundle contains the generated TypeScript Open Perception Kit
SDK and FlatBuffers runtime. The client requires the
`perception-frame-results+base64` encoding marker, validates exact producer
identity, and maps typed payloads into the existing OSD, inference-output, and
performance views. This migration does not change WebUI rendering or controls.

Authored modules live under `development/web/src`; deployable files live under
`development/web/content`. Regenerate, verify, and test the committed bundle
with `./scripts/opksink-web.sh generate`, `check`, and `test` respectively.

## Lifecycle

Initialization constructs the media chains. Each `NULL` to `READY` transition
starts WebRTC, HTTP, then control using the configured properties. `READY` to
`NULL` stops control, HTTP, then WebRTC, closing connections and releasing ports.
Both run before the parent state change. Set new ports while in `NULL`, then
restart the same element to apply them. The services
remain available while paused; restarting from `NULL` requires the application.

The server objects and their status reporters are retained across restarts.
Pending play/pause commands from a stopped control server are discarded. If a
service or the `NULL` to `READY` transition fails, partially started services are
stopped so startup can be retried. Disposal also stops the services, releases
request pads, clears selector references, and frees private state.

Teardown order is important because request pads, selector active pads, and server
threads can otherwise retain references longer than expected.

## Properties

- `host`: HTTP server bind address
- `http-port`: HTTP server port
- `ws-port`: WebRTC signaling port
- `ctrl-port`: control WebSocket port
- `static-files`: static content directory
- `qos-enabled`: enable experimental QoS feedback from the video drain; defaults to `false`

An unset `static-files` property resolves to
`<plugin-directory>/../../web/content`. This is the same plugin-relative
location in development and release builds. An explicit property value
overrides the default. Element setup reports an error when the selected web
root cannot be served.

## Architectural Caveat

`opksink` currently combines media delivery, UI hosting, control, model registry,
and runtime status in one element. This is convenient for demos and development,
but it is not the desired long-term application boundary. See
[Known Limitations](../known-limitations.md).
