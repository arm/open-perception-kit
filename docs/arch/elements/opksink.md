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

The served webpage also has a microphone button that negotiates a separate
audio connection to [opksrc](opksrc.md). Microphone reception and its signaling
server belong to that source element; HTTP hosting remains in `opksink`.
The control WebSocket's `pipeline_state.microphone` flag reports whether an
`opksrc` exists in the pipeline, including nested bins. The microphone button
stays hidden until that flag is true. Losing the control connection or receiving
a false flag hides the button and stops any microphone capture.

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

The browser receives serialized FrameResults records on the metadata WebSocket.
Its committed `opk-web.js` bundle contains the generated TypeScript Open Perception Kit
SDK and FlatBuffers runtime. The client requires the
`perception-frame-results+base64` encoding marker, validates exact producer
identity, and maps typed payloads into the existing OSD, inference-output, and
performance views. Browser overlays use per-frame metadata from the signaling
WebSocket, matched to `requestVideoFrameCallback`'s RTP timestamp. Missing metadata
leaves that frame without overlays. Late metadata applies only while its matching
frame is still displayed; older metadata is never reused.
Browsers without this callback or its RTP timestamp display video without overlays.

Authored modules live under `development/web/src`; deployable files live under
`development/web/content`. Regenerate, verify, and test the committed bundle
with `./scripts/opksink-web.sh generate`, `check`, and `test` respectively.

### Browser Metadata Configuration

The browser loads `metadataPort` and `metadataEndpoint` from `/opk-config.js` to
connect to the metadata WebSocket on the page's host. The `opksink`
`metadata-port` property selects how that configuration is resolved:

- `0` (the default) discovers `opkcomm` elements in WebSocket mode within the
  containing pipeline, including nested bins. Discovery reads their public
  `method`, `ws-port`, and `endpoint` properties.
- With exactly one WebSocket `opkcomm`, the browser uses that element's
  `ws-port` and `endpoint`.
- With no WebSocket `opkcomm`, the browser uses port `8002` and endpoint `/ws`.
- With multiple WebSocket `opkcomm` elements, startup fails until an explicit
  `metadata-port` selects the intended server. The selected publisher must serve
  `/ws`; setting `metadata-port` alone cannot select a custom endpoint.
- An explicit value from `1` to `65535` always takes precedence, including an
  explicit `8002`. It uses endpoint `/ws` and skips discovery.

Resolution happens on each `NULL` to `READY` transition before any `opksink`
server starts. Change ports while the pipeline is in `NULL`, restart it, and
reload the browser page to load the new configuration. Pausing and resuming
does not retarget the browser. `ws-port=0` is not supported by `opkcomm` or
`opksink`; only `metadata-port=0` requests automatic discovery.

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

In the table, **Live** applies without restarting. **NULL/READY** and **NULL**
requests wait for the caller to reach the indicated state. Read-only entries cannot be set.

| Property | Purpose | Default | Accepted values / Example | Changeable in |
| --- | --- | --- | --- | --- |
| `host` | HTTP server bind address. | `0.0.0.0` | Address; e.g. `127.0.0.1` for local HTTP access. | NULL |
| `http-port` | HTTP server port. | `9999` | Integer, 1–65535; e.g. `9090`. | NULL |
| `ws-port` | WebRTC signaling WebSocket port. | `8000` | Integer, 1–65535; e.g. `9000`. | NULL |
| `ctrl-port` | Control WebSocket port. | `8001` | Integer, 1–65535; e.g. `9001`. | NULL |
| `metadata-port` | Browser metadata WebSocket port override. | `0` (discover) | Unsigned integer, 0–65535; e.g. `9002`. `0` enables [automatic discovery](#browser-metadata-configuration). | NULL |
| `static-files` | Static content directory served over HTTP. | Resolved path; see below | Directory path; e.g. `development/web/content`. | NULL |
| `webrtc-stun-server` | STUN URI passed to new WebRTC sessions. | Resolved URI; see below | URI; e.g. `stun://stun.example.com:3478`. | NULL |
| `webrtc-turn-server` | TURN URI advertised to browsers. | Resolved URI or empty; see below | URI; e.g. `turn://user:password@turn.example.com:3478`. | NULL |
| `qos-enabled` | Enable experimental QoS feedback from the video drain. | `false` | `true`, `false` | Live |
| `property-status` | Latest requests, effective values and per-property outcomes. | None until requested | `GstStructure`; see [request API](../../public/how-to/configure-elements.md#requests-and-outcomes). | Read-only |

An unset `static-files` property resolves to
`<plugin-directory>/../../web/content` for development and archive layouts, or
`/usr/share/opk/web` for the Debian multiarch layout. An explicit property value
overrides the default. Element setup reports an error when the selected web root
cannot be served.

STUN defaults to nonempty `OPK_WEBRTC_STUN_SERVER`, otherwise
`stun://<WEBRTC_HOST_IP>:3478` when that host is set, otherwise
`stun://stun.l.google.com:19302`. TURN defaults to nonempty
`OPK_WEBRTC_TURN_SERVER`, otherwise a URI built from `WEBRTC_HOST_IP`,
`OPK_WEBRTC_TURN_USERNAME` and `OPK_WEBRTC_TURN_CREDENTIAL` when all are nonempty,
otherwise an empty string. Read back the instance to obtain its resolved defaults.

Set network, port, static-file and WebRTC server properties in NULL. Servers
and sessions capture those settings at initialization, so they require an
explicit stop to NULL before replacement. This disconnects clients.
`qos-enabled` remains a live control for the internal drain sink.

See [Configure GStreamer elements](../../public/how-to/configure-elements.md)
for discovery, readback, validation and the explicit restart procedure.

## Architectural Caveat

`opksink` currently combines media delivery, UI hosting, control, model registry,
and runtime status in one element. This is convenient for demos and development,
but it is not the desired long-term application boundary. See
[Known Limitations](../known-limitations.md).
