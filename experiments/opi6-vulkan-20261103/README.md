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

# OPi6 Vulkan inference experiment — 3 November 2026

Resumed after the GPU driver update. Both provider builds completed. Vulkan preprocessing passed; ONNX GPU inference hits a reproducible watchdog timeout. This branch preserves experimental source and measurements, not a production backend change.

- [Updated-driver results and remaining blocker](RESUMED.md).
- [Two-minute presentation](../../presentation-dma-vulkan-2026-11-04.md).
- [Original PanVK same-model CPU/GPU matrix](RESULTS.md), [investigation](INVESTIGATION.md) and [optimization research](RESEARCH.md).
- [Separate CPU-only Raspberry Pi threading benchmark](../onnx-threads-20261103/README.md).

## Saved source and evidence

`source-and-evidence.tar.gz` and `snapshot-manifest.json` preserve the original checkpoint unchanged: experimental OPK source, benchmark runners, raw timings, output captures, hardware samples, profiles, build recipes and hashes. Its `mali-tiling.patch` was an unbuilt draft at that checkpoint.

`resumed-source-and-evidence.tar.gz` preserves the work after the driver update: the completed control/candidate build logs and hashes, full candidate patch, current runners, preprocessing results, ONNX failure logs and the small probe-generation script. Its internal `manifest.json` records file hashes. The candidate now compiles; tile numerical correctness, GPU timestamps and performance remain unvalidated because model execution fails.

Neither archive includes downloaded model binaries, licensed video, credentials or local plans. Model acquisition remains owned by OPK's `scripts/download-models.py`; original descriptors and source hashes are in the checkpoint. The scripts retain the dated local paths used during measurement.

Verify the repository artifacts from this directory:

```sh
sha256sum -c SHA256SUMS
mkdir checkpoint resumed
tar -xzf source-and-evidence.tar.gz -C checkpoint
tar -xzf resumed-source-and-evidence.tar.gz -C resumed
```

## Build state

| Component | Version |
| --- | --- |
| OPK benchmark source basis | `c5c1aac6cc3fc3a639e5170d375936130447174d` |
| ONNX Runtime core | 1.31.1 |
| WebGPU source tag | `plugin-ep-webgpu/v0.5.0` |
| WebGPU source commit | `c44523c625e21f713df88e5462d09316298efe28` |
| Pinned Dawn | `v20260916.214021` |

The clean control was saved before applying the candidate. The local source now contains the candidate, so rebuilding it does not produce a clean control. Both libraries are under `mali-tiling-20261103/lib/{control,candidate}/` within:

```text
/home/sanmeg01/.cache/opk-benchmarks/opi6-four-stage-20261102/webgpu-yolo26-20261103/
```

The incremental cross-build uses the existing OPK container and sysroot:

```sh
docker start opk-opi6-four-stage-build-20261102
docker exec opk-opi6-four-stage-build-20261102 bash -lc '
  source /opt/opk-venvs/devtools/bin/activate
  cd /bench/webgpu-yolo26-20261103/mali-tiling-20261103/onnxruntime
  python tools/ci_build/build.py @../build-options.txt --build
'
```

The original runner defaults to a private PanVK ICD. The resumed runner explicitly selects `/etc/vulkan/icd.d/mali.json`. Use the resumed runner for the updated system; confirm loaded libraries before collecting new data. Do not overwrite the colleague's driver with archived libraries.

## Binary and media recovery

The pre-update board/runtime/media backup and laptop source/build cache remain at:

```text
C:\Users\sanmeg01\OPK-backups\opi6-gpu-20261103\
```

`board-benchmark-and-dependencies.tar.gz` contains the old board tree, private runtimes and video inputs, relative to `/home/opi6`. `laptop-experiment-cache.tar.gz` contains the old local cache, relative to `/home/sanmeg01/.cache/opk-benchmarks`. Their existing `SHA256SUMS` verifies those archives. `resumed-builds/` additionally holds the completed control and candidate binaries, their provenance and the current patch.

Restore archives into an empty directory before copying anything into an existing installation. After the resumed tests, no task-owned GPU benchmark remains running; all CPU governors are restored to `schedutil` and the DMA heap to `0600 root:root`. Native macOS, Windows/WSL GPU execution and other GPUs remain untested.
