# ampsink
## WebRTC Streaming Bin for AMP Pipelines

`ampsink` is a `GstBin` element that encapsulates a full audio/video encoding
and WebRTC delivery stack inside a single GStreamer component.

<img src="resources/img/webrtc.jpg" alt="WebRTC utilization" width="600">

It accepts raw video and optional raw audio, encodes them (VP8 for video,
Opus for audio), and exposes the media streams through integrated WebRTC
signaling, HTTP serving, and control channels.

This document provides a high-level architectural overview.
The implementation is extensive and significantly more complex than what
is described here. Many internal details, edge cases, and lifecycle
considerations are intentionally omitted for clarity.

## Purpose

`ampsink` solves the problem of delivering containerized GStreamer pipelines
to a browser reliably and with low latency.

It provides:

- VP8 video encoding
- Opus audio encoding
- WebRTC signaling and transport
- Embedded HTTP server for static UI content
- WebSocket-based control interface
- Model registration and pipeline state tracking
- Performance overlay state inspection

It is designed primarily for containerized or headless deployments
where direct display/audio output is not feasible.

## Element Type

- Base class: `GstBin`
- Video sink pad: `videosink` (always present)
- Audio sink pad: `audiosink` (request pad)
- Output: WebRTC (network transport, not a standard pad)

`ampsink` internally constructs and manages a complete sub-pipeline.

## Video Path Architecture

The internal video chain is:

- video/x-raw  
- videoconvert  
- queue  
- vp8enc  
- identity (clock sync)  
- tee  

One branch of the tee feeds the WebRTC subsystem.
A second branch feeds a fakesink (drain branch) to allow the pipeline
to remain in PLAYING state even when no WebRTC clients are connected.

Key configuration aspects:

- VP8 encoder configured for low latency (`deadline=1`)
- Periodic keyframes (`keyframe-max-dist`)
- Explicit clock synchronization via identity element

The drain branch is critical to prevent pipeline stalls
when no active consumers are present.

## Audio Path Architecture

The audio subsystem is always created internally and guarantees
a valid audio stream even if no external audio is connected.

Internal structure:

- audiotestsrc (silence)  
- input-selector  
- audioconvert  
- audioresample  
- capsfilter (S16LE, 48kHz, stereo)  
- opusenc  
- identity (clock sync)  
- tee  

Two possible sources feed the selector:

1. Silence source (always available)
2. Real audio input (request pad)

If no external audio pad is requested, silence remains active.
If a real audio pad is requested, the selector switches to it.

As with video, a drain branch (tee → queue → fakesink)
prevents pipeline blocking when no WebRTC clients are attached.

## Request Pad Model (Audio)

The audio pad is a `GST_PAD_REQUEST`.

When `audiosink` is requested:

- A ghost pad is created referencing the internal audio queue sink.
- The input selector switches from silence to real input.
- Audio state is reported via `PipelineStateReporter`.

When released:

- The ghost pad is removed.
- The selector can revert to silence.

This design guarantees pipeline stability independent of client connections.

## WebRTC and Control Infrastructure

ampsink embeds substantial non-GStreamer infrastructure:

### WebRTC WebSocket Server

- Handles WebRTC signaling
- Negotiates peer connections
- Coordinates media session lifecycle

### Control WebSocket

- Accepts runtime commands
- Registers and exposes status reporters
- Enables remote interaction

### HTTP Server

- Serves static frontend content
- Hosts the WebRTC-based browser player

These components are started during element initialization and
stopped during disposal.

## Status Reporting

ampsink integrates a reporting abstraction via `StatusReporter`.

### PipelineStateReporter

Reports:

- Current GStreamer state (PLAYING or not)
- Whether real audio input is active

### PerformanceOverlayStateReporter

Discovers `ampperformance` in the top-level pipeline.
Reports:

- Whether performance overlay exists
- Whether it is enabled

These reporters are registered with the control WebSocket subsystem.

## Model Registration Events

ampsink listens for custom downstream events:

- `amp-model-register`
- `amp-model-unregister`

These events originate from `ampinfer`.

Upon reception:

- Model information is added to or removed from an internal registry.
- The registry is exposed through the control channel.

This enables dynamic visibility of active inference models.

## Lifecycle Management

### Initialization

During `gst_amp_sink_init`:

- Internal video and audio chains are constructed.
- WebRTC and control subsystems are started.
- HTTP server is started.
- Status reporters are registered.

### Disposal

During `dispose`:

- HTTP and WebSocket servers are stopped.
- Request pads are released.
- Selector references are cleared.

### Finalization

- Properties are freed.
- Private data structures are deleted.

Proper teardown order is critical due to selector active-pad references
and request pad ownership.

---

## Properties

- `host` — HTTP server bind address
- `http-port` — HTTP server port
- `ws-port` — WebRTC signaling port
- `ctrl-port` — control WebSocket port
- `static-files` — static content directory

All properties are runtime configurable.

## Design Characteristics

- Composite bin architecture
- Explicit drain branches for non-blocking behavior
- Integrated WebRTC signaling
- Runtime status inspection
- Dynamic model registry integration
- Optional audio with silence fallback
- Low-latency encoding configuration

## Important Notes

The implementation is large and multi-layered.

This document does not cover:

- Detailed WebRTC negotiation internals
- HTTP routing implementation
- Threading model of WebSocket servers
- ModelRegistry internals
- Failure recovery paths
- Detailed state transition timing
- Edge cases around pad renegotiation


