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

# OPi6: ONNX GPU inference after Mali kernel tuning

Measured 3 November 2026. The same FP32 YOLO models now run faster on the OPi6 GPU than on its CPU in all six cases. Complete-video throughput improves by 53–99% with the tested CPU settings. The experiment uses native ONNX Runtime WebGPU → Dawn → Vulkan, with the updated Arm driver and two source patches. It is not yet a production OPK backend.

## Complete-video throughput

Medians of three runs, all 417 frames of `GettyImages-1140581459.mov`, with the first 30 frames excluded from steady-state statistics. CPU preprocessing and CPU result handling remain in both paths; GPU transfers are included.

| Model | CPU FPS | GPU FPS | GPU gain |
| --- | ---: | ---: | ---: |
| yolo26n-320 | 20.90 | 34.27 | +64.0% |
| yolo26n-480 | 12.89 | 19.76 | +53.2% |
| yolo26n-640 | 8.92 | 14.14 | +58.6% |
| yolo26s-320 | 9.57 | 19.01 | +98.6% |
| yolo26s-480 | 5.10 | 9.95 | +95.2% |
| yolo26s-640 | 3.28 | 6.50 | +98.4% |

The pipeline decodes the video, converts it to NV12, copies it into a DMA-BUF allocation, runs CPU preprocessing and inference, parses detections, serializes results to a file and finishes at an unpaced sink. It excludes encoding, browser display and network transport. Preserving the DMA-backed video allocation does not make the inference tensors GPU-resident. FPS uses post-warm-up sink-buffer completion intervals; startup, final metadata drain and teardown are outside that timer. Acceptance still requires all 417 serialized frame records.

| Model | CPU FPS range | GPU FPS range | CPU inference ms in pipeline | GPU inference ms in pipeline |
| --- | ---: | ---: | ---: | ---: |
| yolo26n-320 | 20.36–22.04 | 34.17–34.65 | 38.11 | 23.46 |
| yolo26n-480 | 12.09–13.36 | 19.74–19.77 | 67.71 | 43.64 |
| yolo26n-640 | 8.75–8.96 | 14.14–14.15 | 101.14 | 61.92 |
| yolo26s-320 | 9.35–11.65 | 18.93–19.05 | 93.30 | 46.92 |
| yolo26s-480 | 4.96–5.14 | 9.93–9.96 | 186.86 | 93.53 |
| yolo26s-640 | 3.26–3.31 | 6.50–6.51 | 294.97 | 144.99 |

Ranges show all three runs. CPU scheduling on this heterogeneous board gives more variation than the GPU path. CPU inference uses the existing 11-IntraOp/1-InterOp configuration; affinity and thread counts were not exhaustively tuned. These gains compare the measured configurations, not the fastest possible CPU implementation. A short process-activity check found no competing workload during that interval; it does not explain all CPU variation.

## Inference measured separately

These measurements use decoded frames from the same video. The timer covers synchronous inference, host/GPU tensor transfers and all output readbacks. Decode and image preprocessing are outside the timer. Each trial has 30 warm-up calls, at least 50 measured calls and at least 10 seconds of measured inference; values are medians of three trials with alternating CPU/GPU order.

| Model | CPU mean ms | GPU mean ms | CPU inference FPS | GPU inference FPS |
| --- | ---: | ---: | ---: | ---: |
| yolo26n-320 | 26.55 | 23.91 | 37.67 | 41.82 |
| yolo26n-480 | 59.74 | 43.44 | 16.74 | 23.02 |
| yolo26n-640 | 100.33 | 61.45 | 9.97 | 16.27 |
| yolo26s-320 | 75.59 | 46.96 | 13.23 | 21.29 |
| yolo26s-480 | 187.88 | 93.32 | 5.32 | 10.72 |
| yolo26s-640 | 312.79 | 145.08 | 3.20 | 6.89 |

The GPU advantage grows with model size and resolution in the focused measurement. Pipeline speedups differ because decode, preprocessing and inference share CPU resources. Subtracting these independently measured medians would not isolate transfer cost.

## What fixed execution and improved performance

The updated driver initially failed a small first-convolution probe containing layout conversion. The failure depended on Dawn/Tint's generated workgroup-memory initialization. Emitting short initialization loops as a bounded sequence of stores made the probe and complete models run. The patch retains zero initialization, the final partial-group bounds check and the synchronization barrier; loops longer than 16 rounds keep the original form. The initialization index check covered 7,215 cases. The full upstream Tint test suite was not run.

Disabling initialization helped isolate the failure, but is **not enabled** in the measured build. This evidence identifies the generated initialization path; it does not establish the precise defect inside the driver/compiler.

The second patch tunes FP32 convolution and matrix-multiplication workgroups for Mali. The selected `8x32-r1` variant uses an 8×32 workgroup and one output row per invocation. Small matrices, the first RGB convolution and unsupported paths keep generic kernels. Lower accumulator work per invocation may reduce register pressure and improve occupancy; no hardware counters were captured to prove that explanation.

The following short control uses the **same final provider binary** and initialization fix for both configurations. Each cell is one focused screening trial, separate from the repeated CPU/GPU tables above.

| Model | Generic kernels | Selected Mali tile | Latency reduction |
| --- | ---: | ---: | ---: |
| yolo26n-320 | 36.591 ms | 24.009 ms | 34.4% |
| yolo26s-640 | 243.246 ms | 144.657 ms | 40.5% |

This measures the tile's contribution independently of the driver and initialization change. The archive also contains the wider candidate sweep.

## Where time remains

A separate nano-320 profile places all 127 convolutions and four matrix multiplications on WebGPU. Across the optimized graph, 287 nodes use WebGPU and ten output-processing nodes use the CPU. The CPU tail includes shape/index operations, casting, gathering and tiling.

The median profiled model call was 24.61 ms. Host trace events labelled `MemcpyToHost` totalled about 10.24 ms per call, but include waiting for queued GPU work. They are **not** a measurement of 10.24 ms of copying. Hardware GPU timestamp instrumentation returned no events in this run, so shader duration, queue stalls and memory bandwidth cannot be separated from this trace. Profile timing is excluded from the performance tables.

Further work, in order of useful evidence:

1. Remove or support CPU tail nodes, then test persistent GPU tensors and graph capture. ORT documents [I/O binding](https://onnxruntime.ai/docs/performance/tune-performance/iobinding.html) and [WebGPU graph capture](https://onnxruntime.ai/docs/execution-providers/WebGPU-ExecutionProvider.html); the current mixed-provider graph still needs CPU/GPU synchronization.
2. Share tensors between Vulkan preprocessing and inference through an explicit device/buffer ownership contract. The current experiment measures them separately; it does not demonstrate that integration.
3. Profile GPU counters before choosing layer-specific kernels. [Arm's tuner guidance](https://developer.arm.com/community/arm-community-blogs/b/ai-blog/posts/arm-nn-gpu-inference-with-opencl-tuner) supports measuring launch configurations on the target device. [Arm Compute Library's convolution heuristics](https://github.com/ARM-software/ComputeLibrary/blob/main/docs/user_guide/conv2d_heuristic.dox) illustrate geometry-dependent algorithm selection; they are not Vulkan switches in this provider.
4. Evaluate mixed precision or quantized kernels with a representative accuracy dataset. No precision change is included here. The updated driver advertises cooperative matrices, but this experiment does not implement or validate a kernel using them.

The kernel reports a 1 GHz shader clock and a 900 MHz top-level clock. No overclock was applied. One GHz was also the previous driver's maximum exposed setting; CPU and GPU GHz do not compare throughput. The updated driver failed devfreq registration, so the earlier frequency-control interface is unavailable. Arm's [G720 counter reference](https://arm-software.github.io/libGPUCounters/immortalis-g720_and_mali-g720_counter_reference.html) distinguishes the two clock domains.

## Separate preprocessing result

For 1920×1080 NV12 → 320×320 FP32 RGB CHW, CPU preprocessing takes 0.840 ms and Vulkan takes 0.439 ms: about 1.9× faster. These are medians of three runs on the updated driver, including upload and synchronous completion but excluding decode. All 128,102,400 compared values passed absolute tolerance 0.00001. This helper result is not an additional multiplier for the pipeline FPS above.

## Validation and provenance

- Same six original FP32 ONNX files and all six output tensors on CPU and GPU; publisher revisions and SHA-256 hashes are in `model-manifest.json`. The separate Pi thread benchmark uses optimized INT8 files and is not an absolute CPU/GPU comparison.
- 36 complete-video trials: 15,012 processed frames. All inference/preprocessing scopes, ordered timestamps and serialized packets were present; video memory remained DMA-backed across the OpChain. No GStreamer warnings were accepted.
- Across 18 CPU/GPU video pairs, all counts and classes matched: 59,487 detections over 7,506 frame pairs. Maximum box difference was 0.001771 pixels; maximum confidence difference was 0.000397. Acceptance limits were 0.1 pixel and 0.001 confidence, with explicit non-finite-value rejection. This is numerical agreement on one video, not a labelled accuracy evaluation or bit-exact identity.
- Recorded physical Mali driver mappings during GPU runs. Temperature samples stayed between 40 and 56°C in the main matrices; sampled cooling states remained zero. CPU governors were set to `performance` during trials and restored to `schedutil`; DMA heap permissions were restored to `0600 root:root`.

| Component | Measured version |
| --- | --- |
| Board | OPi6, Mali-G720-Immortalis, ten GPU cores |
| Driver | Arm `r53p0`, system `/etc/vulkan/icd.d/mali.json` |
| Kernel | `6.18.26-cix-sky1-lts` |
| OPK source basis | `c5c1aac6cc3fc3a639e5170d375936130447174d`, archived experimental ONNX adapter |
| ONNX Runtime | 1.31.1, both CPU and GPU |
| Provider source | `c44523c625e21f713df88e5462d09316298efe28`, tag `plugin-ep-webgpu/v0.5.0` |
| Dawn | `v20260916.214021` |
| Final provider SHA-256 | `0d7b21deaa37561aaed448ca5f43ed8779df93d952868e2b0ddad51cefa836d4` |

`optimized-source-and-evidence.tar.xz` contains the runners, both patches, build settings/logs, raw trials, output comparisons, hardware samples, profile and a verified member-hash manifest. `optimized-summary.json` contains the table values. The original source archive supplies the unchanged OPK adapter and base helpers; the updated archive overlays the new runner and provider work. Downloaded models, licensed video and compiled libraries are excluded.

Apply `mali-tiling.patch` in the pinned ONNX Runtime source. Populate the pinned Dawn dependency, then apply `dawn-zero-init.patch` inside `build/Release/_deps/dawn-src`. The Dawn patch is separate because that dependency is outside the ORT Git tree. Rebuild with the archived options and `--build`; rerunning dependency population can overwrite the patched dependency. Select `ORT_WEBGPU_MALI_TILE=8x32-r1` and the final provider library in the archived runner. Keep diagnostic initialization-disabling options unset. The runner currently works around device discovery and verifies physical Mali mappings; explicit hardware-adapter selection remains integration work.

Rebuilding requires the pinned upstream sources and the OPK cross-build container/sysroot recorded in the archives. This branch saves an experimental implementation and reproducible evidence; it does not install the provider into the normal product build. Other GPUs, native macOS/Windows GPU execution and GPU execution inside WSL are untested.

[Presentation](../../presentation-dma-vulkan-2026-11-04.md) · [Archive index](README.md)
