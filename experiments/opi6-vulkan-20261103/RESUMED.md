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

# OPi6 benchmark after the driver update

3 November 2026. Vulkan preprocessing completed; ONNX GPU inference is blocked by a reproducible execution failure. No post-update YOLO GPU FPS or Mali-tiling speedup is claimed.

## Environment and build

The updated system uses Arm Mali r53p0 (`v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5`) through `/etc/vulkan/icd.d/mali.json`, with `libmali.so` loaded from `/opt/cixgpu-pro/lib/aarch64-linux-gnu`. The device reports Mali-G720-Immortalis, ten cores; kernel is `6.18.26-cix-sky1-lts`. ONNX Runtime core is 1.31.1. The new driver exposes cooperative matrices and Vulkan memory-model support.

Both the unmodified WebGPU provider and the candidate were built successfully from `c44523c625e21f713df88e5462d09316298efe28` (`plugin-ep-webgpu/v0.5.0`), using the same saved cross-build configuration. The clean control was saved before applying the candidate. Binary hashes, build logs, build options and the complete candidate patch are in the resumed evidence archive.

The kernel reports failure to register GPU devfreq and continues without it. The root clock summary reports a 1 GHz shader clock and 900 MHz GPU top clock. These are configured/reported clocks, not an independent measurement of silicon frequency. No GPU frequency changes were made. A CPU-to-GPU GHz comparison does not predict inference throughput: the processors perform different amounts of parallel work per cycle.

## Completed preprocessing benchmark

Same native helper binary as the earlier measurement, SHA-256 `4895129f26a659785d912115ebdd2eb73317ef9c4315464041adb0aee6de2417`. Input is all 417 frames decoded from `GettyImages-1140581459.mov` to 1920×1080 NV12. Output is 320×320 FP32 RGB CHW. Timed calls cycle through those real frames; decoding is outside the timer. Each trial runs for at least ten seconds. Three trials per backend alternate CPU/GPU order, with cooldowns and hardware sampling. CPU governors are held at performance during these trials and restored afterward.

| Backend | Median mean ms | Min–max mean ms | Median p95 ms |
| --- | ---: | ---: | ---: |
| CPU, updated system | 0.839538 | 0.839452–0.841705 | 0.851485 |
| Vulkan, updated driver | 0.438985 | 0.430523–0.441301 | 0.481711 |
| Vulkan, previous PanVK at 1 GHz | 1.252723 | 1.240068–1.268830 | — |

Updated Vulkan preprocessing is 1.91× faster than CPU and takes 2.85× less time than the earlier GPU path. Its median upload time is 0.213810 ms; dispatch plus wait is 0.223598 ms. This includes upload and synchronous GPU completion, not decode or inference. It does not establish an application FPS gain or isolate the driver from every other OS change.

Validation compared every output element from all 417 frames: 128,102,400 values, zero mismatches at absolute tolerance 1e-5, maximum error 1.19209289550781e-7. An earlier timing batch overlapped provenance hashing and is excluded; only `preprocess-clean-*` contributes to this table.

## ONNX failure and bounded diagnostics

The packaged provider and the fresh unmodified source build both fail YOLO26n/320 with `Device is lost` while downloading an output. Kernel evidence reports an iterator progress-timer timeout. The download error is where the failure becomes visible; it does not prove that the transfer caused it.

| Diagnostic | Outcome |
| --- | --- |
| Packaged provider, default layout | Device lost |
| One pending dispatch per submission | Device lost |
| NCHW preferred layout | Device lost |
| Fresh unmodified source control | Device lost |
| Original model's first convolution, extracted as ONNX | Failure; validation-enabled run also fails |
| Relu-only ONNX on real video input | Pass; GPU output equals CPU output |
| Candidate with Vulkan memory model disabled | Device lost |
| Candidate with SPIR-V 1.4 disabled | Device lost |
| Candidate with both disabled | Device lost |
| Candidate with `8x8-r2` Mali tile selected | Device lost |
| Native Vulkan preprocessing | Pass, all frames validated |

The extracted convolution retains the original weights and consumes real video-derived tensors. Its failure and the successful Relu/native Vulkan tests narrow the problem to this ONNX/WebGPU compute path on the updated stack. They do not establish whether the defect is in shader generation, driver compilation, synchronization or another interaction. The first RGB convolution deliberately retains the generic kernel in the tiling experiment, so changing later packed kernels cannot remove a failure there.

The two diagnostic switches are opt-in and leave default behavior unchanged. They were motivated by the pinned Dawn source's Arm workarounds; neither fixed this failure. Dawn's [Arm workgroup-initialization workaround](https://dawn.googlesource.com/dawn.git/+/1147935a18b4dc9509d180621486ba78474f0c6b%5E!/) is already present in the pinned source. The [official WebGPU provider documentation](https://onnxruntime.ai/docs/execution-providers/WebGPU-ExecutionProvider.html) describes the provider used here.

The six-model GPU matrix and tile ranking remain uncompleted. Repeating larger models after the smallest convolution fails would not produce a valid comparison. The next useful test is the saved first-convolution reproducer against a corrected driver/provider combination; once that passes, resume same-build control/candidate timings, then validate all video detections before reporting FPS. GPU timestamp instrumentation remains unvalidated.

## Evidence and handover

`resumed-source-and-evidence.tar.gz` contains the new runners, candidate patch, exact build provenance, preprocessing measurements, failure logs and probe-generation script. It excludes model binaries, videos, credentials and plans. The original checkpoint archive remains unchanged. The original [CPU/GPU matrix](RESULTS.md) is explicitly a PanVK result, not a measurement of this updated system.

CPU governors were restored to `schedutil` and `/dev/dma_heap/system` to `0600 root:root`. Native macOS, Windows/WSL GPU execution and other GPUs were not tested.
