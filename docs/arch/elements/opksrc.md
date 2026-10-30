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

`opksrc` is a `GstBin` that receives one browser's microphone and camera through
a shared WebRTC connection. Either device can be enabled independently.
Its always-present `src` pad exposes interleaved mono PCM:
`audio/x-raw,format=F32LE,rate=16000,channels=1,layout=interleaved` by default.
The `rate` property sets the output PCM sample rate in Hz (default `16000`).
Set it while the element is in `NULL`, for example `opksrc rate=48000`.
It applies to both silence and decoded microphone audio. Opus RTP negotiation
continues to use its 48 kHz clock; the receive chain resamples the decoded audio.

## Media path

The receive path is `webrtcbin → rtpopusdepay → queue → opusdec → audioconvert →
audioresample`. An `input-selector` chooses between that path and a live silence
generator, followed by the output caps filter.

The selector stays on silence until decoded microphone PCM arrives.
Disconnecting the signaling client resets the receiver and selects silence again.

## Video output
The always-present `video` source pad supplies BGRA frames at 640×480 with
30 fps caps. Its separate fallback branch is
`videotestsrc pattern=black → input-selector → capsfilter → video`.
The camera receive branch in `video.cpp` is
`rtpvp8depay → queue → vp8dec → videoconvert → videoscale → videorate → capsfilter`.
It scales camera frames to the output size and drops frames above the output rate;
slower cameras retain their cadence. The selector starts on black and selects
camera video only after decoded frames arrive. Disabling the camera selects
black before renegotiation, without interrupting audio. The existing `src` pad
remains the audio output. The video pad can be left
unlinked: video buffers are discarded without stopping the audio branch.

## Browser and signaling

The Mic and Cam buttons on the [opksink](opksink.md) webpage request permissions
independently and share a send-only WebRTC connection. HTTP hosting remains owned
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

The browser sends the offer before trickled ICE. Offers may contain at most one
active sending Opus audio section (48 kHz RTP clock) and one active sending VP8
video section (90 kHz RTP clock), in either order. Inactive and rejected audio/video
sections are accepted for stopped tracks. Other media types and unsupported
active codecs are rejected. Payload types and ICE media indices come from SDP.
The receiver answers with receive-only codec preferences and uses BUNDLE.
Repeated offers on the same WebSocket add or disable tracks. The browser creates
a new transceiver when re-enabling a device; inactive sections remain in SDP.

One WebSocket connection owns the receiver. Additional clients are closed without
replacing the active sender. A track without decoded media for 15 seconds falls
back to silence or black; a connection expecting media is closed if neither
enabled track supplies decoded media for 15 seconds. Silent PCM counts as audio.
Enabling a track starts a new grace period; unchanged offers do not renew it.
An all-inactive session can remain connected. After an offer, pausing the local
pipeline also pauses the timeout.
The timeout is checked every 100 ms, but pending SDP operations can delay it.

Invalid signaling closes only the affected connection. Disconnecting recreates
the receiver's `webrtcbin` so a later client starts with fresh SDP and ICE state.

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
The `opksrc_video_signaling` test sends real VP8 and Opus on one peer, exercises
camera-first negotiation and independent disable/re-enable offers, and checks
black/silence fallback, continued output from the other track, and reconnection.
