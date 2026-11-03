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

# OPi6 Vulkan inference checkpoint — 3 November 2026

Paused at the user's request so a colleague can update the GPU driver. No benchmark or build is running. This side branch is an archival experiment, not a production backend change or a merge-ready feature.

## Saved state

- [Measured results](RESULTS.md): complete original FP32 YOLO26n/s CPU/GPU comparison at 320, 480 and 640, focused repeats, detection agreement, and the subsequent graph/precision experiments.
- [Investigation](INVESTIGATION.md) and [clock diagnostics / optimization research](RESEARCH.md).
- [Mali kernel draft](mali-tiling.patch): five opt-in packed-FP32 convolution/MatMul tile geometries, plus separate GPU timestamp instrumentation. **Not compiled, executed or benchmarked.** The patch passes `git apply --check` against the pinned upstream commit. No Mali-tiling performance improvement is claimed.
- `source-and-evidence.tar.gz`: exact experimental source, benchmark runners, model descriptors, hashes, raw timings, hardware samples, profiles, detection captures, build options/logs and shared harness source. `snapshot-manifest.json` records every archived file's SHA-256. Local planning files are excluded.
- The [two-minute presentation](../../presentation-dma-vulkan-2026-11-04.md) contains the completed measurements only.

The new clock diagnostics are single focused trials, with 30 warm-ups and at least 50 timed calls/10 seconds. YOLO26n/320 took 132.755 / 128.945 / 125.705 ms at 600 / 800 / 1000 MHz. YOLO26s/640 took 837.875 / 829.679 ms at 600 / 1000 MHz. Capping the CPUs near 800 MHz with GPU at 1 GHz gave n/320 136.024 ms. These are clues, not proof of a particular bandwidth or synchronization bottleneck.

## Exact revisions and build state

| Component | Recorded version |
| --- | --- |
| OPK benchmark source basis | `c5c1aac6cc3fc3a639e5170d375936130447174d` |
| ONNX Runtime core | 1.31.1 |
| Packaged native WebGPU provider | 0.5.0 |
| WebGPU source tag | `plugin-ep-webgpu/v0.5.0` |
| WebGPU source commit | `c44523c625e21f713df88e5462d09316298efe28` |
| Dawn source pinned by that build | `v20260916.214021` |
| Measured driver / hardware | PanVK 26.0.5, Mali-G720 MC10, Orange Pi 6 Plus |
| Kernel during completed measurements | `6.18.26-cix-sky1-lts` |

The unmodified source-control build was interrupted intentionally. Its last logged completed step was 1138/1493 in the resumed build; earlier compilation is cached. No finished source-built control library exists. The source checkout remains unmodified and the draft is separate in `candidate-overlay/`. Preserve that separation until the clean control has been built and saved. The eight-job cross-build uses the existing OPK container and sysroot; `build-environment.json`, `build-options.txt` and `arm64-toolchain.cmake` capture its configuration. The source-build configuration differs from the packaged release, so eventual tiling comparisons require the same-build unmodified control.

The build container `opk-opi6-four-stage-build-20261102` is stopped. The local source/build cache is under:

```text
/home/sanmeg01/.cache/opk-benchmarks/opi6-four-stage-20261102/webgpu-yolo26-20261103/
```

## Recovery

The Git branch preserves source and evidence. Downloaded model/runtime binaries, the licensed video and incomplete build outputs are not in Git. They are preserved separately in the Windows-side laptop backup directory:

```text
C:\Users\sanmeg01\OPK-backups\opi6-gpu-20261103\
```

- `board-benchmark-and-dependencies.tar.gz` contains the complete board benchmark directory, both referenced private runtime trees, decoded NV12 input and original video, relative to `/home/opi6`.
- `laptop-experiment-cache.tar.gz` contains the complete dated local experiment cache, including the clean upstream source checkout, candidate overlay and incremental build state, relative to `/home/sanmeg01/.cache/opk-benchmarks`.
- `SHA256SUMS` verifies the archive files. Restore into a separate empty directory for inspection before copying into any existing installation.

Extract the Git checkpoint independently with:

```sh
mkdir checkpoint
sha256sum -c SHA256SUMS
tar -xzf source-and-evidence.tar.gz -C checkpoint
```

The scripts intentionally retain the dated paths used for the measurements. They are not an installation package. Their existing README in the extracted `webgpu-yolo26-20261103/` directory documents the OPK module build and board run commands. Model acquisition remains owned by OPK's `scripts/download-models.py`; `models-source.json` pins the original public sources and hashes. Derived-model scripts and manifests are saved.

On the original laptop, the interrupted clean plugin build can be resumed after restarting the saved container:

```sh
docker start opk-opi6-four-stage-build-20261102
docker exec opk-opi6-four-stage-build-20261102 bash -lc '
  source /opt/opk-venvs/devtools/bin/activate
  cd /bench/webgpu-yolo26-20261103/mali-tiling-20261103/onnxruntime
  python tools/ci_build/build.py @../build-options.txt --build
'
```

Do not apply `mali-tiling.patch` until the clean control library has been saved. The draft runner expects control and candidate libraries at `mali-tiling-20261103/lib/{control,candidate}/libonnxruntime_providers_webgpu.so`. Existing result labels are resumable and must not be reused with different binaries. The newest local runner records plugin hashes; the earlier board copy predates that addition, so deploy source from this checkpoint when resuming.

## Driver update boundary

The saved `run.sh` selects a **private PanVK 26.0.5 ICD by default**. Merely updating the system driver will not change that path. After the update, discover the new system ICD, explicitly select it with `OPK_VULKAN_ICD`, and verify the loaded driver and dependent libraries, Vulkan capabilities, GPU identity and clocks before collecting new measurements. The saved `system-panfrost.json` names `/usr/lib/libvulkan_panfrost.so`; verify that path on the updated image. Do not overwrite the new driver with the archived baseline libraries.

Use a new result directory/labels for the new driver. The old baseline, the new-driver unmodified control and the Mali candidate are separate comparisons. GPU timestamp instrumentation and tile numerical correctness still require hardware validation. Native macOS, Windows/WSL GPU execution and other GPUs remain untested.

## Board handover

No task-owned benchmark process remained at the pause check. All five CPU governors were `schedutil`; CPU minimum/maximum limits were restored to their original policy values (approximately 0.8 GHz minimum, 1.5/1.8 GHz maximum). GPU limits were 72 MHz–1 GHz, and `/dev/dma_heap/system` was `0600 root:root`. No passwords or SSH private keys are included in this checkpoint.
