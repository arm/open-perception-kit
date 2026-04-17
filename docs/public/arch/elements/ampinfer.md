---
sidebar_position: 18
sidebar_label: ampinfer
---

# ampinfer
## GStreamer OpChain Execution Element

`ampinfer` is a GstBaseTransform element responsible for executing
OpChains inside a GStreamer pipeline.
Its main purpose is to run inference or inference cascades.

GStreamer provides the media transport and scheduling. `ampinfer` converts
incoming video buffers into the runtime representation required by the
Op system and executes the configured micro-pipeline for each frame.

The Op system itself is independent of GStreamer. `ampinfer` acts as the
integration layer between media transport and the Op execution engine.

## Purpose

-   Execute declaratively defined OpChains from JSON.
-   Run preprocessing → inference → postprocessing stages per frame.
-   Attach structured inference results into Perception.
-   Enable modular, dynamically loaded inference backends.

## Element Type

-   Base class: GstBaseTransform
-   Processing mode: in-place (transform_ip)
-   Supported caps: video/x-raw, format=BGRA

The implementation currently assumes tightly packed BGRA memory (stride
= width × 4).

If padded stride or multi-planar formats are introduced, buffer access
must be migrated to GstVideoFrame and plane stride must be respected
explicitly.

## Properties

`opchain-path` (string, mandatory)

Filesystem path to the OpChain JSON descriptor.
Typical deployments keep opchains under `/work/config/opchains/<name>/opchain.json` and the
referenced model descriptors under `/work/config/models/<name>/`.

This file defines:

- Ordered Ops
- Shared library groups
- Op attributes

`active` (boolean)

Enables or disables OpChain execution.

`format` (string, default: BGRA)

Declares expected input format.

`infer-id` (string)

Optional logical identifier used to tag the inference element instance in runtime metadata.

## Lifecycle

`start()`

-   Allocates internal C++ members.
-   Loads and initializes OpChain from JSON.
-   Emits a custom downstream event: amp-model-register.

The event contains:

- model-name
- element-name
- active state

`stop()`

-   Frees OpChain members and releases resources.

`set_caps()`

-   Parses and validates incoming caps.
-   Ensures video format is BGRA.
-   Stores GstVideoInfo for frame dimension access.

## Per-Frame Execution Path

1.  If active is false → passthrough.
2.  Map the buffer for read/write access.
3.  Ensure `PerceptionMeta` is attached.
4.  Construct OpChainContext.
5.  Create BitmapView from BGRA frame.
6.  Insert BitmapView as “pipelineVideoFrame”.
7.  Assign pointer to `Perception` for Ops to emit persistent data.
8.  Execute OpChain.
9.  On failure, log the error and abort in the current implementation.

All persistent inference output must be written by Ops into Perception.
`OpChainContext` remains transient and is discarded after execution.


## Perception Integration

`ampinfer` guarantees that a `Perception` object exists for every processed
frame.

This enables: 

- Downstream elements (amptracker, amposd, ampperformance) to access inference results. 
- Multi-stage enrichment across elements. 
- Stable UUID-based parent-child linking of detections.

ampinfer does not interpret inference results; it only executes the
OpChain.

## Memory Model

-   Input frame memory remains owned by GStreamer.
-   BitmapView references frame memory without copying.
-   Ops must not retain references beyond execution scope.
-   Perception persists downstream via metadata.


## Error Handling

If OpChain setup or execution fails:

-   Errors are printed.
-   Current behavior aborts execution.

Production deployments should replace abort behavior with proper
GStreamer error signaling.

## Observability

The element references the global PerformanceTracer.

Ops and runtimes may emit timing keys (e.g., preprocessing, inference,
postprocessing). These are consumed by ampperformance for runtime metric
reporting.
