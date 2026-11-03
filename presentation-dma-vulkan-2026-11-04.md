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

# OPK: DMA-BUF, Vulkan preprocessing and native ONNX GPU inference

Wednesday, 4 November 2026. About 90 seconds of overview, followed by 30 seconds for the results.

**Overview — approximately 90 seconds**

Originally, an OpChain prepared images, ran inference and interpreted results on the CPU. DMA-backed video also needed a bridge into CPU memory, so accessing pixels could introduce a copy before useful processing started.

DMA-BUF support now preserves the original video allocation. Deferred mapping means the CPU maps those pixels only when a stage needs them. Vulkan preprocessing adds a GPU implementation of resizing, colour conversion and normalization.

For inference, the experimental backend loads ONNX models through ONNX Runtime's native WebGPU provider. On this Linux board, Dawn executes the supported operations through Vulkan on the Mali GPU.

We tested YOLO26n and YOLO26s at 320, 480 and 640 pixels. Each CPU/GPU pair uses the identical FP32 ONNX file. The measured pipeline retains DMA-backed video, prepares the input on the CPU, runs inference, and parses the results on the CPU. GPU inference still crosses those tensor boundaries, and some detection-tail operations require CPU fallback.

We checked complete video processing and detection agreement, then measured inference separately. We also tested GPU-resident tensors and a compute-only subgraph with graph capture. Those checks distinguish transfer overhead from the cost of the GPU execution path itself.

**Results — final 30 seconds**

On the OPi6, the current FP32 GPU implementation remained slower at every tested size. Complete-pipeline throughput was 21.6 CPU versus 7.6 GPU FPS for YOLO26n at 320, and 3.25 versus 1.19 FPS for YOLO26s at 640. Keeping tensors on the GPU saved only 3–5%, so transfers alone do not explain the gap. The next target is GPU kernel efficiency.

**Slide table — identical FP32 ONNX files**

| Model | Input size | CPU inference ms | GPU inference ms | CPU pipeline FPS | GPU pipeline FPS |
| --- | ---: | ---: | ---: | ---: | ---: |
| YOLO26n | 320×320 | 26.26 | 126.21 | 21.62 | 7.60 |
| YOLO26n | 480×480 | 60.13 | 233.07 | 13.20 | 4.17 |
| YOLO26n | 640×640 | 96.36 | 356.44 | 9.12 | 2.74 |
| YOLO26s | 320×320 | 85.81 | 255.39 | 10.71 | 3.82 |
| YOLO26s | 480×480 | 177.51 | 523.08 | 5.03 | 1.89 |
| YOLO26s | 640×640 | 286.74 | 829.33 | 3.25 | 1.19 |

Inference latency is the median of three focused runs; pipeline FPS is one complete 417-frame video pass, with 30 warmup frames excluded. The pipeline includes decoding, DMA-BUF upload, CPU preprocessing, inference, parsing and result-file publication; display, encoding and playback pacing are excluded. Every model pair passed the detection-consistency check. These measurements describe this implementation, not a general limit of GPU hardware.

**Optional discussion note, outside the spoken two minutes:** A validated FP32 graph rewrite improved GPU pipeline FPS to 7.85 for n/320 and 1.21 for s/640. FP16 reached 1.35 FPS for s/640 but failed the FP32 detection-consistency check. Neither optimization closed the CPU/GPU gap. Compiler diagnostics point toward register pressure and load/store efficiency as kernel-tuning targets; exact GPU shader timings remain unmeasured.

[Detailed measurements and methods](experiments/opi6-vulkan-20261103/RESULTS.md) · [Pipeline chart](experiments/opi6-vulkan-20261103/pipeline-fps.png) · [Optimization investigation](experiments/opi6-vulkan-20261103/INVESTIGATION.md)

Checkpoint: these measurements use PanVK 26.0.5. Work is paused for the driver update; the Mali tiling draft has not been benchmarked. [Saved experiment](experiments/opi6-vulkan-20261103/README.md).
