---
sidebar_position: 1
sidebar_label: pekinfer
---

# pekinfer

`pekinfer` is a `GstBaseTransform` element that executes an OpChain inside a
GStreamer pipeline. GStreamer provides media transport and scheduling;
`pekinfer` adapts each frame into the OPK runtime model and invokes the configured
micropipeline.

## Element Contract

- Base class: `GstBaseTransform`
- Processing mode: in-place `transform_ip`
- Supported caps: `video/x-raw, format={BGRA,RGB,I420,NV12,YUY2}`
- Main property: `opchain-path`, the JSON descriptor to execute
- Control property: `active`, which enables or disables per-frame OpChain execution
- Experimental property: `qos-enabled`, disabled by default
- Metadata output: `PerceptionMeta`

The implementation currently maps CPU-addressable `GstVideoFrame` buffers and
passes per-plane data and stride into preprocessing. DMA-BUF-backed frames are
detected but rejected until explicit zero-copy support is added.

## Lifecycle

On `start()`, the element allocates internal state and loads the OpChain from
JSON regardless of `active`. Setup failure prevents the element from starting.
After successful setup, it emits a downstream `pek-model-register` event with
model name, element name, and active state.

On `set_caps()`, it validates the supported raw video caps and stores frame dimensions.

On `stop()`, it releases OpChain state and resources.

## Per-Frame Execution

For each active frame:

1. Map the buffer for read/write access.
2. Ensure `PerceptionMeta` is attached.
3. Construct an `OpChainContext`.
4. Add the mapped video frame as `videoFrames["pipelineVideoFrame"]`.
5. Expose the frame's `Perception` object to Ops.
6. Execute the OpChain.

Persistent outputs must be written into `Perception`; `OpChainContext` is
transient and discarded after the execution step.

## Error Handling And Observability

Current setup failures are logged and reported as a GStreamer element error;
they prevent startup. Execution failures are also reported and stop the
affected flow. Other runtime paths still contain abort behavior that should be
replaced with graceful error reporting.

The element participates in global performance tracing. Ops and backends can emit
timing keys that `pekperformance` later publishes.

## QoS Feedback

QoS-aware inference skipping is experimental and disabled by default. Set
`qos-enabled=true` on the active `pekinfer`, enable QoS feedback on the sink,
and disable native `qos` dropping on intervening transforms to try it.

An enabled, active `pekinfer` observes upstream `GST_EVENT_QOS` events on its
source-side event path and consumes them after updating its inference policy.
This prevents earlier decoders from reacting by dropping the video buffer.
Inactive instances and instances without `qos-enabled=true` forward QoS events
toward the active inference element.
When an `UNDERFLOW` event reports positive lateness, active frames skip OpChain
execution only while their running-time is earlier than the recovery point
`event timestamp + lateness`. This ignores small spikes that the next frame has
already recovered from and can skip multiple inference executions after a larger
delay. The original video buffers are still forwarded with `PerceptionMeta`
(newly empty when no upstream result exists), allowing `pektracker` to emit
prediction-only detections in the absence of new inference results. `pekinfer`
posts a standard `GST_MESSAGE_QOS`
for each skip. The policy is source-independent because both live sources and
file playback map PTS onto pipeline running-time. It cannot interrupt inference
already in progress. After each successful execution, it also measures its own
synchronous processing latency. When processing exceeds one frame duration, the
ratio against the negotiated framerate determines how many following buffers
skip inference. Counting buffers keeps dropped-frame timestamps and durations
from resuming inference prematurely.

The first enabled, active `pekinfer` encountered by an upstream QoS event consumes
it. Pipelines with multiple active inference elements therefore need further
scheduling work before this feature can be enabled by default.
