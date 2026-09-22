---
sidebar_position: 7
sidebar_label: Known limitations
---

# Known Limitations

This page captures the main architectural friction points in the current OPK
runtime. Treat these as constraints when extending the system.

## Application Boundary

- `opksink` is useful for demos, but it currently combines WebRTC delivery, HTTP
  serving, static UI hosting, control WebSocket handling, model registry state,
  and pipeline state control in one GStreamer element.
- Browser UI hosting should move out of `opksink`; the runtime should publish
  media and perception results through a stable contract.
- `opkcomm` can publish serialized `FrameResults` packets, but there is no
  standard, versioned application endpoint for perception results yet.

## Security And Lifecycle

- `opksink` control and model-info endpoints are not product APIs. Authentication,
  authorization, input validation, and network exposure need dedicated review.
- `opksink` request pads, thread startup/shutdown, and teardown ordering need more
  lifecycle coverage.

## Perception And Postprocessing Contracts

- New persistent result shapes require a Perception schema update and regenerated
  SDK. Parser, tracker, publishing, or visualization changes are still required
  when those components need to produce or consume the new payload semantics.
- Native custom postprocessing is registered through `GenericPostprocessOp`.
  The Python script Op can inspect inference outputs and append FrameResults,
  but it is supported only in native OPK pipelines inside the official
  containers or from a matching binary release on Debian Trixie. It executes
  trusted code in process and its zero-copy tensor arrays are valid only during
  the current call.
- `opkosd` rendering is hardcoded around known content types, so it is best
  treated as a debugging overlay rather than the long-term visualization layer.

## Runtime Failure Handling

- Several setup, tensor, parser, and per-frame paths still use `assert()` or
  `opk_abort()`.
- Parser and tensor validation should return explicit errors.
- GStreamer elements should report element errors and fail the affected chain or
  buffer gracefully instead of aborting the process.

## Media Support

- Main GStreamer elements negotiate linear CPU video frames in `BGRA`, `RGB`,
  `I420`, `NV12`, and `YUY2`.
- Padded stride, multi-planar drawing details, DMABUF, and zero-copy paths are not handled
  consistently yet.
- `opksink` can transport audio, but audio inference is not integrated.

## Configuration And OpChain Contracts

- Pipeline, Model, and OpChain JSON have versioned schemas and reject unsupported
  versions before execution; see [Configuration Compatibility](../public/concepts/configuration-compatibility.md).
  Static validation does not prove model binary, backend, device, or SDK consumer
  compatibility. These require setup and integration tests with the intended deployment.
- OpChain execution is ordered and supports grouped loops, but richer scheduling
  such as startup-only stages is not represented cleanly.

## Models, Backends, And Platforms

- Build-time `hfDownload` metadata materializes one artifact per descriptor but
  does not provide checksums, license metadata, multi-file bundles, or a
  complete-image gate. Failed downloads are logged and skipped.
- ONNX Runtime is the main working backend. ExecuTorch is experimental, MNN is
  planned, and RKNN/Orion6 is not supported.
- Model performance and accuracy baselines are not published consistently.

## Observability And Quality

- There are no stable performance goals for latency, FPS, CPU, accelerator use,
  or memory consumption.
- `PerformanceMetrics` exposes process-wide timing data, but individual measurement points are
  not yet a user-facing contract.
- Coverage is thin for parser behavior, known inference outputs, and GStreamer
  element lifecycle behavior.

## Packaging And Deployment

- Development and deployment still assume containers, source-tree layout, and many
  hardcoded `/work` paths.
- Cross-built deployment images omit the embedded Python operation module. The
  published release images are built natively for amd64 and arm64 and include
  it with a target-platform Python runtime.
