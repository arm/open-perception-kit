---
sidebar_position: 1
sidebar_label: opkinfer
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


# opkinfer

`opkinfer` is a `GstBaseTransform` element that executes an OpChain inside a
GStreamer pipeline. GStreamer provides media transport and scheduling;
`opkinfer` adapts each frame into the OPK runtime model and invokes the configured
micropipeline.

## Element Contract

- Base class: `GstBaseTransform`
- Processing mode: in-place `transform_ip`
- Supported caps: `video/x-raw, format={BGRA,RGB,I420,NV12,YUY2}`
- Main property: `opchain-path`, the JSON descriptor to execute
- Control property: `active`, which enables or disables per-frame OpChain execution
- Experimental property: `qos-enabled`, disabled by default
- Metadata output: `FrameResultsMeta`
- `copy-input`: copy video into CPU memory; default `false` preserves input DMA-BUFs

The element retains input DMA-BUFs without mapping their pixels. CPU consumers
map them on demand through `gst_video_frame_map`, respecting plane offsets and
strides; the mapping is reused within that frame's OpChain execution. System
memory is mapped when the frame enters the element. The input buffer stays
referenced until execution ends.

OPK accepts ordinary `video/x-raw` and explicit `video/x-raw(memory:DMABuf)`
caps for these formats (`BGRA,RGB,I420,NV12,YUY2`). Explicit DMA-BUF caps require
DMA-BUF-backed memory. `copy-input=true` negotiates ordinary system-memory output,
including before `opksink` for WebRTC.
`DMA_DRM` and tiled inputs remain unsupported; they require upstream conversion.
CPU processing requires readable input memory; a failed CPU mapping stops the
pipeline. GPU processing can import supported DMA-BUFs without a CPU mapping.

## Execution Preferences

The [OpChain execution settings](../../public/concepts/configuration-compatibility.md#migrating-existing-configurations)
select `auto`, `cpu` or `gpu`. An Op's setting overrides its chain stage default;
omission inherits that default, then falls back to `auto`. Explicit `auto` can
override a forced chain default. Forced `gpu` fails if that Op cannot run on the
GPU. This feature adds GPU image preprocessing, not a GPU inference or
postprocessing backend.

GPU image preprocessing supports fixed-shape, single-image RGB float32 tensors
in CHW or HWC layout. It performs crop, resize, channel conversion, packing and
normalization. `auto` keeps CPU preprocessing available for unsupported models,
devices or input buffers. It measures eligible preprocessing combinations using
pipeline output performance, without changing forced execution settings, and
saves the selection under `open-perception-kit/preprocessing/` in the user's
cache directory. Cached choices remain subject to runtime capability checks.

Video storage and model input storage are separate. GPU preprocessing may import
the original DMA-BUF or upload CPU-readable pixels. Its output tensor may be
shared with CPU inference or copied back, depending on the driver and backend.
The selection log distinguishes these transfers. Selecting `gpu` therefore does
not guarantee zero-copy operation; forwarding the original video buffer does not
imply that the inference tensor shares it.

GPU support on Linux, Raspberry Pi Linux and Windows with WSL depends on exposed
devices and driver import/mapping support. Native macOS currently uses CPU
preprocessing; running a Linux container does not establish native macOS GPU
support. To build without the optional GPU module, use
`OPK_GPU_PREPROCESSING=disabled ./scripts/build.sh release false`.

## Lifecycle

On `start()`, the element allocates internal state and loads the OpChain from
JSON regardless of `active`. Setup failure prevents the element from starting.
After successful setup and whenever `active` changes, it emits a downstream
`opk-model-register` event with model identity, active state, and the OpChain's
declared required and provided content types.

On `set_caps()`, it validates the supported raw video caps and stores frame dimensions.

On `stop()`, it releases OpChain state and resources.

## Automatic Upstream Activation

When an active `opkinfer` starts or changes from inactive to active, it sends an
upstream requirement for each content type needed by its OpChain. Every upstream
`opkinfer` whose OpChain provides a matching content type becomes active. A newly
activated provider sends its own requirements, so activation can propagate
transitively through multiple dependent elements.

This propagation only enables elements. Setting a `opkinfer` to inactive affects
that element alone and does not disable its upstream providers, because those
providers may still be required by other active elements.

## Per-Frame Execution

For each active frame:

1. Ensure `FrameResultsMeta` is attached, including on QoS-skipped frames.
2. Evaluate the QoS and processing-latency skip policy.
3. Retain an executable frame as a `VideoFrame`, deferring DMA-BUF CPU mapping.
4. Construct an `OpChainContext`.
5. Add the frame as `videoFrames["pipelineVideoFrame"]`.
6. Expose the frame's `FrameResults` envelope to Ops.
7. Execute the OpChain.

Persistent outputs must be appended to `FrameResults`; `OpChainContext` is
transient and discarded after the execution step.

## Error Handling And Observability

Current setup failures are logged and reported as a GStreamer element error;
they prevent startup. Execution failures are also reported and stop the
affected flow. Other runtime paths still contain abort behavior that should be
replaced with graceful error reporting.

Ops and backends record hierarchical scopes in the process-wide
`PerformanceMetrics` recorder. Runtime clients can read aggregate snapshots or
opt-in span-history CSV, while `opkperformance` derives interval averages from
the aggregates and publishes them without resetting the recorder.

## QoS Feedback

QoS-aware inference skipping is experimental and disabled by default. Set
`qos-enabled=true` on the active `opkinfer`, enable QoS feedback on the sink,
and disable native `qos` dropping on intervening transforms to try it.

An enabled, active `opkinfer` observes upstream `GST_EVENT_QOS` events on its
source-side event path and consumes them after updating its inference policy.
This prevents earlier decoders from reacting by dropping the video buffer.
Inactive instances and instances without `qos-enabled=true` forward QoS events
toward the active inference element.
When an `UNDERFLOW` event reports positive lateness, active frames skip OpChain
execution only while their running-time is earlier than the recovery point
`event timestamp + lateness`. This ignores small spikes that the next frame has
already recovered from and can skip multiple inference executions after a larger
delay. The original video buffers are still forwarded with `FrameResultsMeta`
(newly empty when no upstream result exists), allowing `opktracker` to emit
prediction-only detections in the absence of new inference results. `opkinfer`
posts a standard `GST_MESSAGE_QOS`
for each skip. The policy is source-independent because both live sources and
file playback map PTS onto pipeline running-time. It cannot interrupt inference
already in progress. After each successful execution, it also measures its own
synchronous processing latency. When processing exceeds one frame duration, the
ratio against the negotiated framerate determines how many following buffers
skip inference. Counting buffers keeps dropped-frame timestamps and durations
from resuming inference prematurely.

The first enabled, active `opkinfer` encountered by an upstream QoS event consumes
it. Pipelines with multiple active inference elements therefore need further
scheduling work before this feature can be enabled by default.
