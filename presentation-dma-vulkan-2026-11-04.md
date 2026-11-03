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

Originally, the OpChain prepared images, ran inference and interpreted results on the CPU. DMA-backed video could require a copying bridge before processing.

DMA-BUF support now lets the chain preserve the video allocation. Deferred mapping means waiting until a CPU stage needs the pixels before mapping that allocation into CPU address space. Vulkan preprocessing can resize, convert colours and normalize the image on the GPU.

The experimental inference path loads the ONNX model through ONNX Runtime's native WebGPU provider. Dawn translates its work into Vulkan compute commands on the OPi6's Mali GPU.

After the driver update, we isolated an execution failure in generated shared-memory initialization. The working implementation preserves initialization and changes how those stores are generated. We then tuned convolution and matrix-multiplication workgroups for Mali. The same FP32 model files now run faster on the GPU.

The measured video pipeline still uses CPU preprocessing and result handling, with transfers at the inference boundary. It already improves throughput, but keeping tensors on the GPU between preprocessing and inference needs further integration. We checked every frame's detections and measured both individual stages and complete video processing.

**Results — final 30 seconds**

With the same FP32 models and real video, nano-320 improves from 20.90 to 34.27 frames per second. Small-640 improves from 3.28 to 6.50. All six tested models improve pipeline throughput by 53 to 99 percent. Separately, Vulkan preprocessing takes 0.439 milliseconds against 0.840 on the CPU. That preprocessing gain is not yet included in the GPU inference pipeline.

**Slide table — pipeline FPS, medians of three runs**

| Model | CPU FPS | GPU FPS | GPU gain |
| --- | ---: | ---: | ---: |
| yolo26n-320 | 20.90 | 34.27 | +64.0% |
| yolo26n-480 | 12.89 | 19.76 | +53.2% |
| yolo26n-640 | 8.92 | 14.14 | +58.6% |
| yolo26s-320 | 9.57 | 19.01 | +98.6% |
| yolo26s-480 | 5.10 | 9.95 | +95.2% |
| yolo26s-640 | 3.28 | 6.50 | +98.4% |

All 417 video frames were processed in every run; the first 30 were warm-up. The pipeline includes decode, CPU preprocessing, transfers, inference and result serialization, ending at an unpaced sink. It excludes encoding and browser/network delivery. The CPU uses the existing 11-IntraOp/1-InterOp setting; this was not an exhaustive CPU tuning exercise. GPU inference is experimental and uses the updated Arm driver plus the saved initialization and Mali-kernel patches.

[Methods, focused timings, validation and saved implementation](experiments/opi6-vulkan-20261103/OPTIMIZED.md)
