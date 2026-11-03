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

# GPU performance investigation

Sources checked on 3 November 2026. These notes distinguish implementation evidence from hardware measurements.

Follow-up source lookup located the WebGPU plugin 0.5.0 release tag at `c44523c625e21f713df88e5462d09316298efe28`. Source links below now point to that tag commit. The NuGet package itself omits a source commit; this is a release association, not a reproducible-build attestation. Compiler statistics and baseline execution measurements below come from the packaged binaries. The subsequent Mali experiment builds an unmodified source control and its candidate with identical settings.

The current native path is ONNX Runtime → WebGPU provider → Dawn → Vulkan → PanVK → Mali-G720. It loads ONNX directly; Vulkan itself does not interpret a neural-network graph. The provider chooses and generates compute kernels. Faster GPU hardware therefore does not ensure a faster implementation of every model.

## Evidence and hypotheses

- All 127 convolution and four MatMul nodes are assigned to the physical GPU. The original network contains 80 pointwise convolutions and 47 3×3 convolutions. This is not primarily a heavy-operator CPU-fallback problem.
- Bound I/O reduced full-model latency by roughly 3–5% in the endpoint diagnostics. The compute subgraph remains slower with the CPU detection tail removed and graph capture enabled. These measurements rule out bulk external tensor transfers as the dominant explanation; they do not individually measure memory traffic inside each shader.
- The release source's general convolution path uses an 8×8 workgroup and a generic tiled matrix-multiplication shader. Its separate im2col optimization is restricted to selected Intel architectures. The NCHW convolution path also lacks the NHWC vec4 path. This supports investigating kernel choice and tile/register/memory efficiency on Mali; it does not prove a particular instruction or shader is the bottleneck. [Convolution implementation](https://github.com/microsoft/onnxruntime/blob/c44523c625e21f713df88e5462d09316298efe28/onnxruntime/core/providers/webgpu/nn/conv2d_mm.cc), [im2col device selection](https://github.com/microsoft/onnxruntime/blob/c44523c625e21f713df88e5462d09316298efe28/onnxruntime/core/providers/webgpu/nn/im2col_matmul.cc).
- CPU and GPU use different optimized implementations even with the same ONNX file. An 11-thread CPU reference is not a single-core comparison. The CPU ranges are reported because thread placement is not pinned.
- The host trace's readback spans include waiting for earlier GPU work. They cannot establish that the same duration was spent copying bytes. No isolated GPU shader timestamps were obtained in the plugin traces.

The ONNX export also contains 15 groups of summed pointwise convolutions: 40 small convolutions can be expressed as 15 larger convolutions over concatenated inputs. An experimental graph rewrite preserves each FP32 weight value and bias, reduces the convolution count from 127 to 102, and retains the six original outputs. It requires numerical validation because floating-point reduction order changes. This is a separate optimized graph, not the byte-identical-file baseline. The model publisher documents a CPU-targeted export/optimization flow; the precise reason for this graph decomposition is not established here. [Model card](https://huggingface.co/Arm/yolo26n-320-int8-onnx-raspberrypi5).

## Implementation guidance tested

1. Keep static shapes and reuse the session, allocations and compiled kernels; exclude warmup from timing. Use NHWC as the initial layout and explicitly disable validation overhead for measurements. The baseline already does these. [Native provider options](https://onnxruntime.ai/docs/execution-providers/WebGPU-ExecutionProvider.html).
2. Bind reusable GPU tensors and keep intermediates resident. Use graph capture when every node runs on GPU. Always wait for completed GPU work before reporting latency. The baseline's separate resident diagnostics implement these checks. [I/O binding](https://onnxruntime.ai/docs/performance/tune-performance/iobinding.html).
3. Screen CPU thread count, NCHW layout and a larger pending-dispatch batch while retaining identical FP32 ONNX files. The provider's default dispatch window is 16; the experiment tests 256. [Context defaults](https://github.com/microsoft/onnxruntime/blob/c44523c625e21f713df88e5462d09316298efe28/onnxruntime/core/providers/webgpu/webgpu_context.h), [thread management](https://onnxruntime.ai/docs/performance/tune-performance/threading.html).
4. Test a separately labelled FP16 conversion with FP32 input/output boundaries, retaining the original FP32 baseline. Conversion can reduce memory traffic and arithmetic cost but requires detection validation. [Official conversion guide](https://onnxruntime.ai/docs/performance/model-optimizations/float16.html). Arm's GPU inference guidance also identifies reduced precision, tuned convolution algorithms and kernel tuning as important; its Arm NN results are guidance, not measurements of this Vulkan backend. [Arm guidance](https://developer.arm.com/community/arm-community-blogs/b/ai-blog/posts/making-the-most-of-arm-nn-for-gpu-inference).

If these settings are insufficient, the next engineering target is a Mali-specific kernel profile and convolution/matrix-multiplication tuning. Changing drivers or claiming a hardware limit without that evidence is premature. Vulkan preprocessing interoperability would remove input boundaries, but the measured transfer savings bound how much that alone can improve this implementation.

## Conversion checks

The initial FP16 conversion was rejected before inference: `keep_io_types=True` introduced FP32 output tensors into downstream FP16 convolutions when a graph output was also an internal feature. The corrected converter keeps internal tensors FP16 and adds casts only at the external boundaries. It also repairs original `Cast(to=FLOAT)` attributes that converter 1.16.0 left inconsistent with converted tensor annotations, matching the reported [converter issue](https://github.com/microsoft/onnxconverter-common/issues/320). All six corrected models pass ONNX's full type/shape checker. The unchanged FP32 artifacts are retained.

## Compiler evidence

A separate instrumented run of the original YOLO26n/320 model used the system PanVK driver with `BIFROST_MESA_DEBUG=shaderdb,statsfull` and the disk shader cache disabled. Loader tracing verifies `/usr/lib/libvulkan_panfrost.so`; the uninstrumented system-driver latency was 126.216 ms, essentially matching the isolated driver's 126.081 ms screening result.

The run emitted 39 compiled-shader statistics records. Ten used enough working registers to limit estimated occupancy to 50%; the others were estimated at 100%. Maximum working-register use was 60. All records reported zero register spills/fills, and all static total-path estimates named the load/store unit as the limiting category. These are unique compilation records, not the 287 graph-node executions, and are not weighted by invocation count or runtime duration. A shader's loops and data-dependent work further limit interpretation. This is evidence for investigating register pressure and load/store instructions, not proof of saturated external DRAM bandwidth or a measured 50% GPU utilization.

A concrete next kernel experiment is a Mali-specific convolution/MatMul tile with fewer live accumulators per invocation, balancing occupancy against data reuse. Compare workgroup and tile choices on the actual model shapes, retain activation fusion, and use hardware timestamps/counters to distinguish arithmetic, local-memory, external-memory and synchronization costs. The current release's generic tile has not been demonstrated optimal for Mali. [Mesa compiler statistic definitions](https://gitlab.freedesktop.org/mesa/mesa/-/blob/mesa-26.0.5/src/panfrost/compiler/bifrost/bifrost_compile.c), [Arm G720 counter reference](https://arm-software.github.io/libGPUCounters/immortalis-g720_and_mali-g720_counter_reference.html).

Raw compiler log: `../evidence/shader-stats-system-n320.log`; parsed summary: `../shader-statistics.json`. Debug instrumentation was used only in this diagnostic, not in FPS measurements.

The provider option named `enableMatmulFp32Accumulation` applies to `MatMulNBits` in this release; it is not a general FP32-accumulation switch for the ordinary YOLO convolutions. It is therefore not presented as an available fix for the FP16 candidate's numerical drift. [Provider configuration](https://github.com/microsoft/onnxruntime/blob/c44523c625e21f713df88e5462d09316298efe28/onnxruntime/core/providers/webgpu/webgpu_execution_provider.h).

## Outcome

The combined FP32 graph passed the complete-video detection consistency check at both endpoints. Pipeline throughput improved from 7.60 to 7.85 FPS for YOLO26n/320 and from 1.195 to 1.214 FPS for YOLO26s/640. Each candidate has one full-video pass; these small gains need repeated pipeline runs before treating their exact size as stable.

FP16 reached 7.82 and 1.35 FPS respectively, but failed the original FP32 detection tolerance at both endpoints. At n/320 there were 15 unmatched reference detections and six unmatched candidate detections; at s/640 there were 27 and 26. Matched detections also had substantial coordinate/confidence differences. The cause of that numerical drift has not been isolated between conversion, reduced-precision arithmetic and backend behavior. Ground-truth mAP was not measured, so this is not evidence of a quantified accuracy loss, nor a validated drop-in optimization.

Keep the FP32 reference and investigate GPU kernel tiling first. Any FP16 follow-up needs per-layer numerical comparisons and labelled-data accuracy validation before deployment. See [complete results](RESULTS.md) for timings and checks.
