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

# ONNX CPU thread benchmark on Raspberry Pi 5

> Earlier configuration sweep. The requested baseline → IntraOp → InterOp comparison, using actual cherry-picked merges and all six YOLO models on lecso, is in [the chronological benchmark](../onnx-threads-stages-20261103/README.md).

3 November 2026. On lecso, YOLO26n/320 improved from **20.11 to 21.69 pipeline FPS (7.9%)** with `intraOpNumThreads=2` and `interOpNumThreads=2`. Mean inference time fell from **40.80 to 37.20 ms**. These are medians of three complete real-video runs per finalist.

[Two-minute presentation](../../presentation-onnx-threads-2026-11-04.md) · [All trials](trials.csv) · [Machine-readable summary](summary.json)

## What the settings mean

**IntraOp** divides work inside an ONNX operator, such as convolution, among CPU threads. **InterOp** allows independent operators in the same ONNX graph to run concurrently when their inputs are ready. Graph dependencies still apply. These settings do not make OPK Ops or video frames execute concurrently and do not cap the pipeline's total thread count. See [ONNX Runtime's threading documentation](https://onnxruntime.ai/docs/performance/tune-performance/threading.html).

OPK exposes both as integer attributes of `opk-onnx-ops/Inference`. IntraOp omission retains hardware-thread-count minus one, at least one: three on this Pi. IntraOp zero delegates to ORT; one uses the calling thread; other positive values include the caller. InterOp omission or one selects sequential graph execution. Zero selects parallel execution with an ORT-managed pool; values greater than one select parallel execution with the requested pool size. This is OPK's mapping of the attributes to ORT session options.

The measured candidate is an attribute fragment to place in the existing inference Op:

```json
{
  "intraOpNumThreads": 2,
  "interOpNumThreads": 2
}
```

Each inference session has its own settings. Retune for another model, resolution, board or pipeline; this result does not justify changing every model's defaults.

## Confirmed results

| IntraOp | InterOp | Pipeline FPS, median (range) | Mean inference ms | CPU ms/frame |
| --- | --- | ---: | ---: | ---: |
| omitted → 3 | omitted → 1 | 20.11 (20.08–20.12) | 40.80 | 175.03 |
| 2 | 2 | 21.69 (21.65–21.73) | 37.20 | 159.97 |
| 2 | 0 | 21.66 (21.62–21.74) | 37.21 | 159.69 |
| 2 | 1 | 20.26 (20.18–20.26) | 40.65 | 128.81 |
| 3 | 1 | 20.22 (20.21–20.26) | 40.53 | 174.39 |

Each row above contains three runs. Inference is the `onnx/Infer` scope, excluding preprocessing and parsing. CPU ms/frame sums process CPU time across threads; it is not elapsed frame latency or a power measurement. Two IntraOp threads with sequential execution retain approximately the baseline FPS while using less process CPU time. The two parallel finalists are close enough that their small difference is not a useful performance distinction; explicit 2/2 is the documented candidate.

## Complete screening grid

Pipeline FPS for one full video pass per pair; only the finalists above were repeated. InterOp 2–4 all select parallel execution.

| IntraOp \ InterOp | 0 (auto, parallel) | 1 (sequential) | 2 | 3 | 4 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0 | 13.68 | 16.99 | 13.91 | 13.71 | 13.78 |
| 1 | 17.14 | 16.11 | 17.14 | 17.13 | 17.15 |
| 2 | 21.66 | 20.26 | 21.73 | 21.47 | 21.56 |
| 3 | 18.12 | 20.26 | 18.53 | 18.40 | 18.45 |
| 4 | 13.65 | 17.09 | 14.10 | 13.80 | 13.68 |

More threads did not consistently improve throughput. For example, automatic IntraOp with automatic InterOp achieved only 13.68 FPS in this screening pass. Thread scheduling, synchronization and contention are plausible explanations; this experiment does not independently measure their individual costs.

## Method and checks

- CPU only: software decode, BGRA conversion, preprocessing, ONNX Runtime CPU inference, postprocessing and tracking. Ordinary system memory; no DMA or GPU devices exposed to the task container. Csalamade was busy and was not benchmarked.
- Same `GettyImages-1140581459.mov` as the full ONNX pipeline. All 417 frames reached inference and the final sink in order, with unique PTS. No controlled subset was dropped. Every run produced 417 metadata records and exactly 417 inference calls.
- YOLO26n/320 uses the same optimized INT8 ONNX file in every trial. This is separate from the OPi6 FP32 CPU/GPU comparison; their FPS values are not directly comparable.
- Same pipeline throughout: decode → CPU preprocessing/inference/parsing → tracker → performance metadata → result-file publication → unsynchronized fakesink. Display, WebRTC, encoding and playback pacing are excluded.
- First 30 frames are warmup. FPS uses completion intervals across the remaining 387 frames; inference statistics use their 387 calls. The metadata writer is drained before teardown, outside the FPS interval.
- Baseline plus all 25 pairs from 0 through 4 were screened using real video. The top two sequential and top two parallel pairs, plus baseline, received two additional runs; the second round reversed order. Total: 36 complete runs and 15,012 frames.
- All model payloads match the baseline: structure and discrete fields exactly; numeric values with absolute tolerance 1e-4 and relative tolerance 1e-5. Runtime identities, timing metadata and tracking payloads are excluded from this comparison.
- CPU governor held at performance and restored to `ondemand`. Temperature range 49.4–64.8°C; every sample reports `throttled=0x0`. Other containers consumed no measured CPU during the runs. The task container was stopped afterward.

## Reproducibility and scope

ORT 1.24.4, GStreamer 1.26.2, Raspberry Pi kernel `6.18.34+rpt-rpi-2712`. The board's existing release runtime is based on OPK `2112f6e3e7b0fc95c369a2920688ab06642297bc` plus its saved IntraOp implementation patch. To avoid a large rebuild on the nearly full SD card, this experiment privately backports the current InterOp attribute and session-option behavior into that runtime, and relinks its embedded inference schema. It reuses the repository's recorded compilation and link commands. The original source/runtime remain untouched and mounted read-only.

This compares settings within one benchmark build, not before/after FPS across repository revisions. The archive preserves the original source manifest/patch, backported files, build scripts, logs and binary hashes. Current public configuration behavior is documented in [performance measurement](../../docs/public/how-to/performance-measurement.md).

| Artifact | SHA-256 |
| --- | --- |
| YOLO26n INT8 model | `a64a843197d4e7697c2d834d610fba88a7ebd81a62f39d1270ea7ad1aed687fc` |
| Video | `ea5960f81984b6ad4fc59b402de7b22a08ba297666a2dc3f3538ac18bf9be45a` |
| Benchmark ONNX Ops library | `948bdf1180d9d8903645e0a4e300bd0402ea4fe26bbc890aaecb247c6eb76081` |
| Benchmark common library | `df1ebfefdaa669f5e624efacd842eb894605d37312a62ffc51d446d1b87c952f` |

`source-and-evidence.tar.xz` contains all 36 valid trials, hardware samples and the exact harness/backport source. Initial harness-validation attempts are excluded from the results. No models, video or credentials are included. `manifest.json` inside the archive verifies its contents. Native macOS, Windows/WSL and other CPUs were not benchmarked. The Markdown files were checked with repository hooks; full documentation generation could not run because host Doxygen is unavailable.
