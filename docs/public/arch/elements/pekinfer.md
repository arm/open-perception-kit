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
- Supported caps: `video/x-raw, format=BGRA`
- Main property: `opchain-path`, the JSON descriptor to execute
- Control property: `active`, which enables or disables OpChain execution
- Metadata output: `PerceptionMeta`

The implementation currently assumes tightly packed BGRA memory with stride equal
to `width * 4`. Padded stride, multi-planar formats, and zero-copy paths require
explicit `GstVideoFrame`/plane-stride handling.

## Lifecycle

On `start()`, the element allocates internal state, loads the OpChain from JSON,
and emits a downstream `pek-model-register` event with model name, element name,
and active state.

On `set_caps()`, it validates BGRA caps and stores frame dimensions.

On `stop()`, it releases OpChain state and resources.

## Per-Frame Execution

For each active frame:

1. Map the buffer for read/write access.
2. Ensure `PerceptionMeta` is attached.
3. Construct an `OpChainContext`.
4. Add the BGRA frame as `bitmapViews["pipelineVideoFrame"]`.
5. Expose the frame's `Perception` object to Ops.
6. Execute the OpChain.

Persistent outputs must be written into `Perception`; `OpChainContext` is
transient and discarded after the execution step.

## Error Handling And Observability

Current setup and execution failures are logged and may abort execution. Product
paths should replace abort behavior with proper GStreamer error reporting.

The element participates in global performance tracing. Ops and backends can emit
timing keys that `pekperformance` later publishes.
