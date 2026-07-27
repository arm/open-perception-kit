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

On `start()`, the element allocates internal state, loads the OpChain descriptor
from JSON, and emits a downstream `pek-model-register` event with model name,
element name, and active state. It does not configure the OpChain or load a model
at this point.

On `set_caps()`, it validates BGRA caps and stores frame dimensions.

The first active frame starts OpChain setup on a background worker. That frame
and later frames pass through unchanged until setup is ready; inference
execution is synchronous after setup completes. Inactive models do not start
setup.

If setup fails, the element reports one GStreamer warning and remains in
pass-through mode. Deactivating and reactivating a failed model permits another
setup attempt. Deactivation does not interrupt setup already in progress, but
pipeline teardown requests cooperative cancellation and joins the setup worker.

On `stop()`, it cancels any in-progress model materialization and releases
OpChain state and resources.

## Per-Frame Execution

For each active frame, the element first starts or polls asynchronous setup. If
the OpChain is not ready, it passes the frame through without further work.
Once the chain is ready, it:

1. Ensures `PerceptionMeta` is attached.
2. Maps CPU-direct buffer memory for read access.
3. Constructs an `OpChainContext`.
4. Adds the BGRA frame as `videoFrames["pipelineVideoFrame"]`.
5. Exposes the frame's `Perception` object to Ops.
6. Executes the OpChain.

Persistent outputs must be written into `Perception`; `OpChainContext` is
transient and discarded after the execution step.

## Error Handling And Observability

Setup failures are logged, reported once as a GStreamer warning, and leave
`pekinfer` in pass-through mode. OpChain execution failures are reported as
GStreamer element errors and fail the affected buffer.

The element participates in global performance tracing. Ops and backends can emit
timing keys that `pekperformance` later publishes.
