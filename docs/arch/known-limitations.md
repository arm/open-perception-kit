---
sidebar_position: 7
sidebar_label: Known limitations
---

# Known Limitations

This page captures the main architectural friction points in the current OPK
runtime. Treat these as constraints when extending the system.

## Application Boundary

- `peksink` is useful for demos, but it currently combines WebRTC delivery, HTTP
  serving, static UI hosting, control WebSocket handling, model registry state,
  and pipeline state control in one GStreamer element.
- Browser UI hosting should move out of `peksink`; the runtime should publish
  media and perception results through a stable contract.
- `pekcomm` can publish serialized `Perception`, but there is no standard,
  versioned application endpoint for perception results yet.

## Security And Lifecycle

- `peksink` control and model-info endpoints are not product APIs. Authentication,
  authorization, input validation, and network exposure need dedicated review.
- `peksink` request pads, thread startup/shutdown, and teardown ordering need more
  lifecycle coverage.

## Perception And Postprocessing Contracts

- New `Perception` result structures still require coordinated C++ changes across
  the common model, serializer, parser, and visualization code.
- Custom postprocessing is C++-only today and registered through
  `GenericPostprocessOp`.
- `pekosd` rendering is hardcoded around known content types, so it is best
  treated as a debugging overlay rather than the long-term visualization layer.

## Runtime Failure Handling

- Several setup, tensor, parser, and per-frame paths still use `assert()` or
  `pek_abort()`.
- Parser and tensor validation should return explicit errors.
- GStreamer elements should report element errors and fail the affected chain or
  buffer gracefully instead of aborting the process.

## Media Support

- Main runtime elements assume linear, tightly packed BGRA video frames.
- Padded stride, multi-planar formats, DMABUF, and zero-copy paths are not handled
  consistently yet.
- `peksink` can transport audio, but audio inference is not integrated.

## Configuration And OpChain Contracts

- JSON model, OpChain, and pipeline schemas need stronger documentation,
  validation, and tests.
- Descriptor versioning and migration rules are not defined.
- OpChain execution is ordered and supports grouped loops, but richer scheduling
  such as startup-only stages is not represented cleanly.

## Models, Backends, And Platforms

- ONNX Runtime and HailoRT are the main working backends. ExecuTorch is
  experimental, MNN is planned, and RKNN/Orion6 is not supported.
- Model performance and accuracy baselines are not published consistently.

## Observability And Quality

- There are no stable performance goals for latency, FPS, CPU, accelerator use,
  or memory consumption.
- `PerformanceTracer` and `pekperformance` exist, but measurement checkpoints are
  not yet a user-facing contract.
- Coverage is thin for parser behavior, known inference outputs, JSON/schema
  validation, and GStreamer element lifecycle behavior.

## Packaging And Deployment

- Development and deployment still assume containers, source-tree layout, and many
  hardcoded `/work` paths.
- Binary distribution of runtime components, model descriptors, and OpChains is
  not ready.
- Release-ready container images and deterministic artifacts are still needed.
