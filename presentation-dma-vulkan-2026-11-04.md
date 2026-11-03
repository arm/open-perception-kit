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

# OPK: DMA-BUF, Vulkan preprocessing and ONNX GPU inference

Wednesday, 4 November 2026. About 90 seconds of overview and 30 seconds for results.

**Overview — approximately 90 seconds**

Originally, the OpChain prepared images, ran inference and interpreted results on the CPU. Reading DMA-backed video could also require a bridge into CPU memory before processing began.

DMA-BUF support preserves the video allocation. Deferred mapping means the CPU maps the pixels only when a stage needs them. Vulkan preprocessing can resize the image, convert its colour format and normalize it on the GPU.

The experimental inference backend loads the ONNX model through ONNX Runtime's native WebGPU provider. On the OPi6, Dawn translates the GPU operations into Vulkan compute work. CPU stages and unsupported operations can still introduce transfers, so this is not yet an entirely GPU-resident pipeline.

We use frames from the same real video as the full ONNX pipeline. We measure individual stages separately from complete-pipeline throughput and compare their outputs. That tells us whether a change makes the computation faster and whether it improves the application.

After the driver update, native Vulkan preprocessing works and matches the CPU output. ONNX inference currently fails with a GPU watchdog timeout, including a test containing just the model's first convolution. The Mali-specific kernels compile, but their performance cannot be validated until this execution failure is resolved.

**Results — final 30 seconds**

For a 1080p video frame converted to a 320-by-320 input tensor, CPU preprocessing takes 0.840 milliseconds. Vulkan takes 0.439 milliseconds, including upload and completion: about 1.9 times faster. The previous GPU driver took 1.253 milliseconds with the same helper. These are preprocessing times, not application FPS. There is no valid post-update YOLO GPU FPS result yet.

**Slide table — preprocessing only**

| Execution | Mean time per image, median of three runs | Range of run means |
| --- | ---: | ---: |
| CPU, updated system | 0.840 ms | 0.839–0.842 ms |
| Vulkan, previous PanVK driver, 1 GHz | 1.253 ms | 1.240–1.269 ms |
| Vulkan, updated Arm driver | 0.439 ms | 0.431–0.441 ms |

The helper reads 417 decoded GettyImages video frames, then cycles through them during timed trials. Decode is outside the timer. Input is 1920×1080 NV12; output is 320×320 FP32 RGB CHW. GPU time includes input upload and synchronous completion. All 128,102,400 output values passed the CPU comparison at absolute tolerance 0.00001. No full-pipeline speedup follows directly from this table.

[Updated-driver evidence and remaining blocker](experiments/opi6-vulkan-20261103/RESUMED.md) · [Historical same-model CPU/GPU matrix on PanVK](experiments/opi6-vulkan-20261103/RESULTS.md)
