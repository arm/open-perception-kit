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

# ONNX threading: chronological benchmark on lecso

Measured 3 November 2026. CPU only, Raspberry Pi 5, six optimized INT8 YOLO26 models, real video. The stages are **baseline → IntraOp → InterOp**, with the actual feature merges cherry-picked in their original order and no intervening runtime features. ONNX Runtime 1.31.1 is held constant across all three source stages.

The later InterOp merge improves nano-model pipeline FPS by 8.3–10.6% relative to the IntraOp stage. The small-320 two-to-three-IntraOp-thread control improves FPS by 6.5%. That change is an IntraOp preset adjustment included in the InterOp merge; small-320 still uses sequential graph execution.

## Pipeline FPS, step by step

Medians of three complete-video runs per cell. Percentages use unrounded medians. The final column compares against the original baseline, which already used three IntraOp threads.

| Model | Baseline FPS | + IntraOp FPS | + InterOp FPS | IntraOp step | InterOp step | Total vs baseline |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| yolo26n-320 | 18.05 | 17.80 | 19.28 | -1.4% | +8.3% | +6.8% |
| yolo26n-480 | 9.58 | 9.10 | 10.06 | -5.0% | +10.6% | +5.0% |
| yolo26n-640 | 5.53 | 5.38 | 5.93 | -2.6% | +10.2% | +7.3% |
| yolo26s-320 | 10.04 | 9.41 | 10.04 | -6.3% | +6.7% | 0.0% |
| yolo26s-480 | 4.92 | 4.91 | 4.90 | -0.2% | -0.1% | -0.3% |
| yolo26s-640 | 2.70 | 2.69 | 2.80 | -0.1% | +4.0% | +3.9% |

These are changes from each source stage with its committed presets. They combine configuration support with the preset choices in that merge. Merely exposing a setting does not make inference faster.

| Model | Baseline Intra/Inter | IntraOp stage Intra/Inter | InterOp stage Intra/Inter |
| --- | ---: | ---: | ---: |
| yolo26n-320 | 3/1 | 2/1 | 2/2 |
| yolo26n-480 | 3/1 | 2/1 | 2/2 |
| yolo26n-640 | 3/1 | 2/1 | 2/2 |
| yolo26s-320 | 3/1 | 2/1 | 3/1 |
| yolo26s-480 | 3/1 | 3/1 | 3/1 |
| yolo26s-640 | 3/1 | 3/1 | 3/2 |

The IntraOp stage selects two threads for nano and small-320. Under this fixed runtime and full-video workload, its measured FPS medians are lower than the original three-thread policy. The nano-320 run ranges overlap, so its small decrease should not be treated as a precise regression estimate. The InterOp stage recovers that loss and improves nano throughput through parallel graph execution. Small-320 returns to the baseline's three-thread configuration; its two-to-three-thread gain should not be presented as an equally large net improvement over the baseline. Small-480 is a useful unchanged-setting control.

## Rechecking small-320 and ticket EXPKITS-1360

This control uses the final InterOp source for both settings, with InterOp fixed at one. The two-thread trials run after the main matrix; the three-thread values are the final-source trials from that matrix. Each setting has three complete-video repetitions.

| Same source, different IntraOp setting | Median pipeline FPS | Median mean inference ms |
| --- | ---: | ---: |
| Final source, IntraOp 2 / InterOp 1 | 9.43 | 90.52 |
| Final source, IntraOp 3 / InterOp 1 | 10.04 | 84.04 |

The measured gain is **6.5%**. The [ticket attachment](https://jira.arm.com/secure/attachment/1739172/onnx-thread-measurements.md) reported 9.60 → 10.37 FPS, about 8%. Its board was csalamade with 16 GB RAM and it used the first 120 frames, with 30 warm-up frames. This run uses lecso with 8 GB and all 417 frames, with the same 30-frame warm-up. Both use ONNX Runtime 1.31.1 and three repeats. Board, video coverage, source revision and pipeline context differ, so the percentages are not expected to be identical. The observed direction agrees; the new result does not reproduce exactly 8%.

## Source isolation

| Stage | Source |
| --- | --- |
| Baseline immediately before IntraOp | `1a51b4f327436077f3daa32636ab000591732bf8` |
| Cherry-picked IntraOp merge #178, 22 October | Original `13243a122ce7ec87337cc5c1f60e63bb75a408c4` → snapshot `0d316dee037704171c5ccdc66a4321f737d3dc46` |
| Cherry-picked InterOp merge #233, 2 November | Original `c50ef33f3316f82d3b2c670bf5c0f3b8ec8888c3` → signed snapshot `ed66d5b6e0bd18d73ad4c167287f6deb78648ce2` |

The IntraOp snapshot's tree matches the original merge exactly. InterOp's ONNX runtime implementation hunks apply unchanged. Conflicts concern context introduced by intervening audio/Vulkan work: absent audio model configurations remain absent, unrelated test/doc context is omitted, and InterOp's reader version and thread tests are retained. The archive contains the exact range diff and runtime patch comparison.

The InterOp binary was built at `1687cba0b91502749e29b75478e50947ee8237b7`. Adding the required commit signature produced `ed66d5b6e0bd18d73ad4c167287f6deb78648ce2` with the identical source tree `9dfb0375f29c88b556e43e5093585ba7e075fa6e`. Raw trial manifests retain the build-time commit. Both saved feature snapshots have verified SSH signatures and DCO trailers.

All stages use the repository release build wrapper, the same GCC 14 cross compiler/sysroot and **ONNX Runtime 1.31.1**. This holds the runtime constant to isolate the feature merges. It does not reconstruct the original IntraOp implementation's ONNX Runtime 1.24.4 environment. All six model descriptors and model-file hashes match across stages.

## What the settings mean

**IntraOp** divides work within one operator, for example a convolution, among threads. **InterOp** runs independent operators in the same model graph concurrently. Operators still wait for their inputs. Neither setting controls concurrency between video frames.

Configure the attributes on an `opk-onnx-ops/Inference` Op in its model-local `opchain.json`:

```json
{
  "intraOpNumThreads": 2,
  "interOpNumThreads": 2
}
```

IntraOp includes the calling thread. If omitted, OPK keeps its hardware-based policy: one fewer than the reported hardware threads, with a minimum of one. InterOp omitted or set to one selects sequential graph execution; values above one select parallel execution. Zero lets ONNX Runtime choose the corresponding pool size and, for InterOp, selects parallel execution. More threads can add synchronization and compete with decode, preprocessing and postprocessing. See the [ONNX Runtime threading reference](https://onnxruntime.ai/docs/performance/tune-performance/threading.html).

## Method and validation

The source is `GettyImages-1140581459.mov`, used by the full ONNX pipeline examples. The shared pipeline uses the corresponding YOLO preset from the InterOp merge: decode, BGRA conversion, CPU preprocessing, inference, detection parsing, tracking, performance metadata, file serialization and 960×540 scaling. Playback rate limiting is removed and the sink is unpaced. Encoding, browser display and network transport are outside this benchmark.

Each trial processes all 417 frames, excluding the first 30 from steady-state statistics. FPS uses sink-buffer completion intervals after warm-up; startup, final metadata drain and teardown are outside that timer. Acceptance still requires all 417 serialized frame records. Stage/model order reverses in the second repetition. All processing remains on the CPU in an unprivileged, network-disabled container with no GPU devices mounted. The container image is `sha256:0afe28c82597869a7163cb506e6b800cafc5ce4ec15f9c2acd5ae9779c6ec164`; process maps verify that each trial loaded the staged ONNX module and ONNX Runtime 1.31.1.

All 57 measured trials passed: 54 stage trials plus three small-320 controls, 23,769 video frames. Every trial has 417 ordered unique timestamps, 417 inference scopes and 417 serialized result packets. Decoded model detections match the baseline with absolute tolerance 0.0001 and relative tolerance 0.00001. Structural/non-floating fields match exactly; runtime identities, timing and tracker state are excluded. The baseline smoke trial is retained as evidence but excluded from all tables.

The CPU governor was `performance` during the batch and restored afterward. Hardware sampling checks temperature, ARM clock, throttling, swap counters and other-container CPU consumption. No throttling or monitor errors were accepted; other-container CPU use stayed below 0.1 core. The 909 samples recorded 47.2–67.5°C; all samples after selecting the performance governor were approximately 2.4 GHz. Other-container CPU consumption was zero in the recorded intervals. Swap counters increased by one page read and zero pages written. Existing unrelated containers were left untouched, and the task container was stopped after copying the results. See `validation-summary.json` for the recorded ranges and restoration checks.

## Runtime and variation

For nano-320, the IntraOp stage lowers process CPU time from 201.21 to 144.92 ms/frame, while FPS falls slightly. This shows the throughput/CPU-use tradeoff behind the thread-count choice.

The FPS range shows all three trials; inference columns are medians of each trial's mean and p95. CPU time is summed across threads and can exceed elapsed frame time. It is not a power measurement.

| Model | Stage | FPS range | Mean inference ms | Inference p95 ms | Process CPU ms/frame |
| --- | --- | ---: | ---: | ---: | ---: |
| yolo26n-320 | baseline | 17.97–18.41 | 39.65 | 41.02 | 201.21 |
| yolo26n-320 | intra | 17.79–18.12 | 40.66 | 41.38 | 144.92 |
| yolo26n-320 | inter | 19.25–19.68 | 36.05 | 37.12 | 185.53 |
| yolo26n-480 | baseline | 9.54–9.59 | 87.58 | 89.70 | 354.68 |
| yolo26n-480 | intra | 9.09–9.10 | 93.14 | 94.75 | 255.92 |
| yolo26n-480 | inter | 10.03–10.07 | 82.65 | 84.79 | 331.56 |
| yolo26n-640 | baseline | 5.52–5.58 | 161.90 | 164.43 | 587.40 |
| yolo26n-640 | intra | 5.38–5.41 | 166.82 | 169.33 | 409.38 |
| yolo26n-640 | inter | 5.92–5.94 | 150.09 | 153.48 | 541.89 |
| yolo26s-320 | baseline | 10.04–10.06 | 83.98 | 85.57 | 339.26 |
| yolo26s-320 | intra | 9.40–9.43 | 90.74 | 91.86 | 247.65 |
| yolo26s-320 | inter | 10.03–10.04 | 84.04 | 85.62 | 340.10 |
| yolo26s-480 | baseline | 4.90–4.93 | 186.36 | 188.72 | 653.35 |
| yolo26s-480 | intra | 4.90–4.95 | 186.76 | 189.03 | 655.41 |
| yolo26s-480 | inter | 4.90–4.92 | 186.99 | 189.78 | 655.91 |
| yolo26s-640 | baseline | 2.69–2.70 | 351.88 | 354.84 | 1157.01 |
| yolo26s-640 | intra | 2.69–2.72 | 352.29 | 355.49 | 1157.40 |
| yolo26s-640 | inter | 2.80–2.81 | 334.67 | 356.45 | 1417.36 |

## Saved evidence

`source-and-evidence.tar.xz` contains the chronological Git bundle and patch, stage configuration/SDK snapshots, build logs and library hashes, runner source, all frame/timing results, hardware samples and a verified SHA-256 member manifest. Downloaded models, video, compiled libraries and local plans are excluded. Reuse OPK's existing artifact download owners for the model/video files identified in the manifest.

The isolated source is also pushed as [`feature/EXPKITS-1360-lecso-inter`](https://github.com/arm/open-perception-kit/tree/feature/EXPKITS-1360-lecso-inter). The bundle contains the two signed cherry-picked commits and requires baseline `1a51b4f327436077f3daa32636ab000591732bf8`, available in the repository history. Extract it and import its branch into an isolated checkout to recover the measured source. Use the archived release-build recipe and fixed runtime paths when rebuilding.

```sh
sha256sum -c SHA256SUMS
mkdir evidence
tar -xJf source-and-evidence.tar.xz -C evidence
git bundle verify evidence/lecso-stages-20261103/chronological-stages.bundle
```

`summary.json` contains the table medians/ranges and `trials.csv` the individual runs. This benchmark validates Linux on lecso; macOS, native Windows, WSL and other boards were not tested for this chronological comparison. It does not establish power savings or model accuracy on a labelled dataset.

[Two-minute presentation](../../presentation-onnx-threads-2026-11-04.md)
