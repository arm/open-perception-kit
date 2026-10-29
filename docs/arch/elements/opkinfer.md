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
- Supported video caps: `video/x-raw, format={BGRA,RGB,I420,NV12,YUY2}`
- Supported audio caps: interleaved raw U8, native-endian S16/S32/F32, or S24LE,
  1–384000 Hz and 1–64 channels
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
select `cpu` or `gpu` for all preprocessing Ops in the chain; omission defaults
to `cpu`. There are no per-Op overrides. A `gpu` request fails if any preprocessing
Op cannot run on the GPU, so mixed CPU/GPU preprocessing requires separate chains.
Inference remains with the backend selected by its Op ID and model descriptor;
MLWL manages its own Vulkan device. Postprocessing runs on CPU.

GPU image preprocessing supports fixed-shape, single-image RGB float32 tensors
in CHW or HWC layout. It performs crop, resize, channel conversion, packing and
normalization. Select `cpu` when the model, device or input buffer is unsupported.
Execution settings remain fixed while the pipeline runs.

The Vulkan preprocessor uses push constants when the device can hold its 160-byte
parameter block, otherwise it uses a uniform buffer. Push constants avoid a mapped
parameter buffer and repeated shader reads from it. Independently, it selects
16×16 compute workgroups when the device's workgroup limits permit them, otherwise
8×8. Both shader variants use specialization constants to set the selected
workgroup dimensions when creating the pipeline. Both paths wait for GPU
completion before exposing the output tensor to the inference backend.

Workgroup size affects scheduling, memory locality and how many groups can run
concurrently within register/shared-memory limits. Larger groups can reduce
scheduling overhead, but can also reduce concurrency or waste work at image
edges. Fewer groups alone do not establish a speedup.

For tuning this image shader, use the following starting points:

1. Start with **8×8 (64 invocations)**. Try **16×8 or 8×16 (128)**, then
   **16×16 (256)**; compare shapes because neighboring pixels' memory access
   matters. With substantial shared memory or barriers, try smaller groups first.
2. Check `maxComputeWorkGroupInvocations` and each `maxComputeWorkGroupSize`
   dimension before testing a candidate. Prefer multiples of the device's compute
   subgroup width where known; the largest legal group is not necessarily fastest.
3. Keep shader `local_size`, host dispatch counts and capability checks consistent.
   Round dispatch counts up and retain bounds checks for partial edge groups.
4. Change only workgroup size/shape while comparing repeated GPU timestamp
   measurements on representative frames and output sizes. Keep parameter delivery,
   clocks and other shader code fixed; verify output correctness and total
   preprocessing throughput separately. Retune after substantial shader changes.

The current 16×16 choice improved the tested Mali-G720 workload; it is not a
universal optimum. See [Arm's compute guidance](https://support.arm.com/documentation/101897/0304/Compute-shading)
and [Vulkan workgroup limits](https://docs.vulkan.org/guide/latest/compute_shaders.html).

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
`./scripts/build.sh release false --extra-setup-args=-Dgpu_preprocessing=disabled`.

## Lifecycle

On `start()`, the element allocates internal state and loads the OpChain from
JSON regardless of `active`. OpChain loading failure prevents the element from starting.
After successful setup and whenever `active` changes, it emits a downstream
`opk-model-register` event with model identity, active state, and the OpChain's
declared required and provided content types.

GPU device setup runs asynchronously. On the first inference frame of each stream,
an upstream latency query identifies live input. Live inputs keep forwarding frames
without inference until setup finishes. Offline inputs (and inputs that cannot answer
the query) wait for setup before executing inference, preserving the first frame.
GPU setup errors are reported when inference next runs and stop the affected flow.

On `set_caps()`, it validates raw video or audio caps and caches the media format.
Audio caps also validate the PCM inference window and configured hop.

On `stop()`, it releases OpChain state and resources.

## PCM Audio Scheduling

`audio-hop-samples` controls the distance between consecutive inference windows in
converted mono 16 kHz samples. Its default, `0`, uses the window size. Set the
property in NULL or READY; it cannot change while streaming.

The window is derived from the bound inference Ops' `AudioPcm` input shape,
excluding any context supplied by the preceding audio preprocessor.
Accepted shapes have a fixed positive final sample dimension and singleton
preceding dimensions, such as `[512]` or `[1, 512]`. All PCM inputs in the chain
must agree on the window size. Audio caps fail negotiation for a missing PCM
input, dynamic/batched shapes, log-mel inputs, a window larger than the ring, or
a hop larger than the window. Log-mel window calculation is not implemented.
PCM scheduling also rejects any nonzero Op `loopId` in the chain before executing
a window. The scheduler repeats the entire chain for each window; generic Op loops
remain available for video and custom execution paths. Repeating an input audio
file does not create an Op loop and is unaffected by this restriction.

The element converts and buffers audio before scheduling. It waits for a full
window, executes the entire OpChain, advances the read position by the hop, and
repeats while another complete window is available. With a 512-sample window
and default hop, 1500 converted samples produce two executions and leave 476
samples for the next input buffer. A smaller hop creates overlapping windows;
choose it only when the model's streaming semantics support overlap. Audio
execution bypasses the video QoS frame-dropping policy.

Each execution receives a borrowed `PcmRingBuffer::View` named
`pipelineAudioData` in `OpChainContext::pcmViews`. Its absolute sample positions
identify the window within the current stream. `inferenceInfo.audio` carries
these positions, the effective `hopSampleCount`, and optional window presentation
timestamps. Timestamps are anchored to source PTS, compensated for resampler
delay, and never replaced with wall-clock time. Missing initial timestamps can
be recovered from later PTS; a timestamp gap larger than one source sample resets
history. Windows containing negative resampler preroll have absent timestamps. Ops must not retain the view
beyond the execution. This control path does not fill model tensors: audio
preprocessing remains responsible for consuming the supplied window. Results
from executions share the incoming buffer's `FrameResultsMeta`; postprocessors
must preserve earlier window results when appending their output.

Stream starts, segment/flush resets, caps changes, discontinuous or GAP buffers,
and inactive periods clear the audio history and scheduling position. The first
subsequent window sets `inferenceInfo.audio.discontinuity`; stateful audio Ops
must use this flag to reset their own state. Model state is not reset by the
scheduler itself. Stop/start recreates the entire OpChain.

The ring retains up to 60 seconds of converted PCM. An input buffer that
would overwrite unprocessed audio fails explicitly instead of silently skipping
windows. EOS does not infer from a partial window or drain the converter's
filter tail. These are streaming control semantics, independent of any VAD model.

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

OpChain loading failures are logged and reported as a GStreamer element error;
they prevent startup. GPU device setup and execution failures are reported during
processing and stop the affected flow. Other runtime paths still contain abort behavior that should be
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

### PCM tensor preprocessing

Place `opk-std-ops/GenericAudioPreprocess` immediately before the audio inference
Op in an unlooped chain. It copies the scheduled mono Float32 16 kHz window into
an `AudioPcm` Float32 input with shape `[N]`, `[1, N]`, or further singleton
leading dimensions. By default the window contains exactly `N` samples. With `contextSampleCount=C`,
the scheduler requests `N-C` fresh samples and the Op prepends the last `C` samples
from the preceding execution. Context starts at zero and resets on discontinuity.
This mode requires contiguous windows and a hop of `N-C`; the default hop uses
that fresh sample count. Timestamps and sample positions describe only the fresh
window. The Op does not resample or generate log-mel features.

`inputAudioSourceName` defaults to `pipelineAudioData`. `inputAudioTensorIndex`
selects an input explicitly; omitted or `-1` requires a unique `AudioPcm` input.
Other model inputs, including recurrent state, are untouched. Scheduler timing
and discontinuity metadata are preserved.

The Silero INT8 model at `config/models/silero-vad/` uses 512-sample fresh windows
(32 ms) plus 64 retained context samples and an unlooped preprocess → inference → postprocess chain. Its model
artifact is downloaded from the pinned Hugging Face revision in `model.json`.
The ONNX graph receives Float32 PCM `[1,576]`, Float32 state `[2,1,128]`,
and an Int64 scalar sample rate of 16000 through a `Value` input. For a PCM WAV recording:

```sh
gst-launch-1.0 filesrc location=/path/to/audio.wav ! wavparse ! \
  opkinfer opchain-path=/work/config/models/silero-vad/opchain.json audio-hop-samples=512 ! \
  opkcomm file-name=/work/var/silero-vad-results.jsonl ! fakesink sync=false
```

`opkinfer` converts supported input PCM rates to 16 kHz itself. ONNX resets the
inputs targeted by tensor feedback to zero on `inferenceInfo.audio.discontinuity`, before
running inference; the audio tensor is preserved. Silero's parser resets its
hysteresis state at the same boundary. Feedback is copied after each successful
inference. A final incomplete window is not padded or inferred at EOS.

### Audio media boundary

`mediaio::AudioFrame` exposes original-format, CPU-readable interleaved PCM through
one `DataView`, with sample type, channel count, sample rate, samples per channel,
and presentation timestamp. Missing timestamps use `InvalidTimestampNs`.
`mediaio::gst::GstAudioFrame` owns the GStreamer buffer reference and read mapping;
it rejects unsupported formats, planar layouts, and partial sample frames.
The view borrows that mapping and remains valid only for the frame lifetime.
Mapping introduces no explicit PCM copy, although GStreamer may consolidate
non-contiguous memory internally.

`opkinfer` feeds the frame to `PcmRingBuffer`, then releases the mapping before
updating downstream result metadata. The ring still owns conversion, downmixing,
resampling, and accumulation as mono Float32 at 16 kHz. Frame construction performs
no conversion. Non-GStreamer callers can implement `AudioFrame` or use the raw-byte
`feed()` overload. The element retains caps/event handling, timestamp continuity,
active-state checks, and inference scheduling.
