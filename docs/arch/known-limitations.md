---
sidebar_position: 7
sidebar_label: Known limitations
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

- `opkcomm` file and FIFO outputs reject symlinks in every path component and
  reject other file types before opening them for I/O. They require Linux 5.6
  or newer and permission to use `openat2`. Existing outputs also require
  procfs mounted at `/proc`. In sticky directories covered by `fs.protected_regular`,
  existing regular files whose reported owner is the overflow UID are rejected:
  distinct unmapped owners can appear as that same UID in a user namespace.
- `opksink` control is not a product API. Authentication,
  authorization, input validation, and network exposure need dedicated review.
- `opksink` request pads, thread startup/shutdown, and teardown ordering need more
  lifecycle coverage.

## Perception And Postprocessing Contracts

- New persistent result shapes require an Open Perception Kit schema update and regenerated
  SDK. Parser, tracker, publishing, or visualization changes are still required
  when those components need to produce or consume the new payload semantics.
- Native custom postprocessing is registered through `GenericPostprocessOp`.
  The Python script Op can inspect inference outputs and append FrameResults,
  but it is supported only in native OPK pipelines and Python-hosted GStreamer
  applications inside the official containers or from a matching binary release
  on Debian Trixie. A Python host must already use the compatible OPK runtime.
  It executes trusted code in process and its zero-copy tensor arrays are valid
  only during the current call.
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
- `opkinfer` accepts linear, CPU-mappable DMA-BUF input through GStreamer, including
  padded strides and separate planes. Preprocessing still creates separate model
  input tensors; this is not end-to-end zero-copy inference. Tiled/compressed
  DMA-BUF layouts and non-mappable device memory require upstream conversion.
- `opkinfer` supports interleaved raw PCM input, converted to mono Float32 at
  16 kHz with up to 60 seconds of buffered history. Inference runs on complete
  PCM windows; `audio-hop-samples=0` defaults to the required fresh sample count.
  The Silero VAD ONNX chain uses 512 fresh samples (32 ms) plus 64 context samples
  and emits speech probability, speech-active state, sample bounds, and optional
  presentation timestamps through `VoiceActivityDetections` in `FrameResults`.
- Audio inference currently requires an unlooped chain with fixed PCM input
  shapes and a common fresh window size. Dynamic/batched inputs, log-mel
  preprocessing, and planar PCM input are unsupported. The hop cannot exceed the
  window; Silero's context requires contiguous, nonoverlapping 512-sample hops.
  EOS does not pad or infer a partial window or drain the resampler's filter tail.
  See [PCM Audio Scheduling](elements/opkinfer.md#pcm-audio-scheduling) for the
  supported formats, timestamp handling, and discontinuity resets.

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
