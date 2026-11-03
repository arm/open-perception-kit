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

# Mali-G720 inference tuning research — 2026-11-03

This experiment retains the original FP32 YOLO26 ONNX files and ONNX Runtime 1.31.1. The native WebGPU plugin dispatches through Dawn to Vulkan/PanVK on OPi6. Research suggestions below are not measured improvements unless the experiment report says so.

## GPU frequency

The board's devfreq table exposes 72, 216, 350, 600, 800 and 1000 MHz; its configured maximum is 1000 MHz. Thus 1 GHz is normal for this firmware/driver and is the highest exposed setting. It is not an arbitrary low benchmark cap. These measurements do not establish the silicon's absolute limit or independently measure each internal clock domain. No overclocking was used. See `board-frequencies.txt` and the sampled `hardware-clock-*.json` files.

CPU and GPU GHz do not compare throughput: GPUs distribute work over many arithmetic lanes, typically at lower frequency. Arm describes this energy-efficiency tradeoff in its [GPU inference guidance](https://developer.arm.com/community/arm-community-blogs/b/ai-blog/posts/making-the-most-of-arm-nn-for-gpu-inference). The [G720 counter reference](https://arm-software.github.io/libGPUCounters/immortalis-g720_and_mali-g720_counter_reference.html) also distinguishes shader-core and top-level clock domains; their relationship is implementation-dependent.

The small-model screening measured 132.755 ms at 600 MHz, 128.945 ms at 800 MHz and 125.705 ms at 1000 MHz. Each is one focused trial with 30 warm-up calls and at least 50 timed calls/10 seconds, with actual frequency sampled. The 67% frequency increase reduced completed inference latency by 5.3%. This points away from a simple arithmetic-throughput limit, but does not by itself distinguish memory behavior, GPU synchronization, submission overhead or internal clock-domain behavior.

The larger YOLO26s/640 trial measured 837.875 ms at 600 MHz and 829.679 ms at 1000 MHz, a 1.0% latency reduction. Both completed 50 timed calls after 30 warm-ups, with 41+ seconds of timed inference. This makes a small-model-only explanation unlikely.

A host-frequency diagnostic held the GPU at 1 GHz and capped all CPU policies at their approximately 800 MHz minimum. YOLO26n/320 changed from 125.705 to 136.024 ms (+8.2%). This confirms some host sensitivity, but neither CPU nor exposed GPU clock scaling accounts for most latency. This single-trial diagnostic does not replace GPU timestamps or memory-system counters.

## Kernel tiling and algorithm selection

[Arm's tuner guidance](https://developer.arm.com/community/arm-community-blogs/b/ai-blog/posts/arm-nn-gpu-inference-with-opencl-tuner) advocates measuring kernel launch configurations on the target device. [nnJIT research](https://www.microsoft.com/en-us/research/publication/empowering-in-browser-deep-learning-inference-on-edge-devices-with-just-in-time-kernel-optimizations/) independently demonstrates the value of device-specific kernel tuning for WebGPU-class execution. Its models and devices differ from this experiment; its published speedups are not predictions for YOLO26.

The experimental sweep varies packed FP32 output rows per thread and workgroup height in both pointwise MatMul and spatial convolution. It retains vectorized four-channel outputs, the existing accumulation order and generic behavior for unsupported cases. Workgroup sizes are multiples of the observed 16-lane subgroup. Smaller per-thread tiles can reduce register use but can also increase loads and workgroup count, so selecting a tile requires measured whole-model latency and output comparison.

A next algorithmic candidate is a separate direct or Winograd path for suitable 3×3 convolutions. [Arm Compute Library's own convolution heuristic documentation](https://github.com/ARM-software/ComputeLibrary/blob/main/docs/user_guide/conv2d_heuristic.dox) uses layer geometry, data layout and accuracy constraints to choose between direct convolution, GEMM variants and Winograd. That is OpenCL guidance, not an existing Vulkan switch: applying it here requires a kernel implementation and numerical validation. NHWC is already enabled in this benchmark; the earlier NCHW trial was slower.

## Other neural-network optimizations

- **Reduce intermediate tensors and dispatches.** Prefer producer/consumer fusion where it saves materialized tensors without excessive registers. The original graph already has many fused activations; remaining Add/activation sequences and layout operations need profiling. The earlier FP32 pointwise-convolution combination gave a small validated gain, not a solution to the CPU/GPU gap.
- **Mixed precision with measured accuracy.** Half-width data reduces storage and traffic, and can improve arithmetic throughput. The earlier all-FP16 experiment changed detections, so a sensible follow-up keeps sensitive operations or accumulation in FP32 and validates against a labelled dataset. [ORT's mixed-precision guidance](https://onnxruntime.ai/docs/performance/model-optimizations/float16.html) requires representative inputs and accuracy checks. The plugin's `enableMatmulFp32Accumulation` option applies to a specialized quantized MatMul kernel, not general convolution.
- **Quantized convolution using packed dot products.** Live Vulkan capabilities expose accelerated packed 4×8-bit integer dot products. The examined WebGPU provider has a DP4A path for specialized quantized MatMul, but does not register QLinearConv/ConvInteger. Loading an INT8 YOLO file therefore does not automatically produce optimized GPU convolution. This needs kernel/operator support and calibration; [ORT's quantization guide](https://onnxruntime.ai/docs/performance/model-optimizations/quantization.html) explains the accuracy tradeoff.
- **Persistent buffers and graph replay.** [ORT documents I/O binding](https://onnxruntime.ai/docs/performance/tune-performance/iobinding.html) and [WebGPU graph capture](https://onnxruntime.ai/docs/execution-providers/WebGPU-ExecutionProvider.html). Their applicability is already measured: resident input/output saved only a few percent; CPU tail operators prevent full-graph capture. A compute-only capture probe remained slower than CPU. Reusing allocations and reducing unnecessary internal synchronization may still help, but eliminating external transfers alone will not close the gap.
- **Use actual GPU counters before naming a memory bottleneck.** Compiler occupancy/load-store estimates are static. G720 counters distinguish queue stalls, load/store demand, cache behavior and external-memory stalls. No such hardware-counter capture has been made in this experiment. The GPU timestamp diagnostic measures dispatch duration, not cache misses or DRAM bandwidth.

The current Vulkan driver does not advertise cooperative-matrix support. A matrix-accelerator kernel is therefore not an available shortcut in this stack. Native Windows/macOS GPU backends and other Mali devices are not covered by this OPi6 experiment.
