---
sidebar_label: opksrc
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

# opksrc

`opksrc` is a `GstBin` that receives one browser microphone through WebRTC.
Its always-present `src` pad exposes interleaved mono PCM:
`audio/x-raw,format=F32LE,rate=16000,channels=1,layout=interleaved` by default.
The `rate` property sets the output PCM sample rate in Hz (default `16000`).
Set it while the element is in `NULL`, for example `opksrc rate=48000`.
It applies to both silence and decoded microphone audio. Opus RTP negotiation
continues to use its 48 kHz clock; the receive chain resamples the decoded audio.

## Media path

The receive path is `webrtcbin → rtpopusdepay → opusdec → audioconvert →
audioresample`. An `input-selector` chooses between that path and a live silence
generator, followed by the output caps filter.

The selector stays on silence until decoded microphone PCM arrives.
Disconnecting the signaling client resets the receiver and selects silence again.

## Browser and signaling

The microphone button on the [opksink](opksink.md) webpage requests permission
and opens a separate, send-only WebRTC connection. HTTP hosting remains owned
by `opksink`. Use localhost or an SSH tunnel to access the webpage.

`opksrc` listens for plain WebSocket signaling on `ws-port` (default `8003`,
range `1–65535`). Configure the property while the element is in `NULL`; changes
take effect at the next startup. The browser uses the page hostname and port
8003, with an optional `window.OPK_CONFIG.micWsPort` override.

The signaling protocol matches the browser client:

- Browser offer: `{"type":"offer","sdp":"..."}`.
- Receiver answer: `{"type":"answer","sdp":"..."}`.
- ICE in either direction:
  `{"type":"candidate","ice":{"candidate":"...","sdpMLineIndex":0}}`.

The browser sends the offer before trickled ICE. Offers must contain one active
sending audio media section with Opus at a 48 kHz RTP clock rate. Payload types
are taken from SDP, rather than fixed to a particular number. The receiver
answers with a receive-only Opus transceiver.

One WebSocket connection owns the receiver until it disconnects or sends no
decoded audio for 15 seconds. Silent PCM counts as audio; signaling traffic does
not renew the deadline. A local pause suspends the media deadline after an offer.
The watchdog checks every 100 ms; pending SDP operations can delay it.
Additional clients are closed without replacing the active sender. Invalid signaling
closes only the affected connection. Disconnecting recreates the receiver's
`webrtcbin` so a later client starts with fresh SDP and ICE state.

## Lifecycle

The WebSocket server starts during `NULL → READY` and stops during
`READY → NULL`. Shutdown closes clients and releases the listening port.
Bind failures are reported as GStreamer errors and can be retried after returning
to `NULL`. The server remains available while the pipeline is paused.

Signaling and peer teardown are coordinated with element state changes.
Outstanding SDP operations have bounded waits, and ICE callbacks retain only
weak references to the session.

## Verification

The `opksrc_signaling` integration test negotiates against a real GStreamer Opus
sender, checks decoded audio, reconnects using different RTP payload numbers,
and covers stalled peers, malformed messages, concurrent clients, shutdown,
and bind failures.
The `opksrc_audio` test checks the silence output contract and the receive path.
