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

The updated Arm driver now runs all six FP32 YOLO models successfully with the saved Dawn initialization fix and Mali-specific kernels. Three repeated real-video runs per model/provider show 53–99% higher pipeline FPS on the GPU with the tested CPU configuration. The source, patches, raw evidence and two-minute presentation are saved on this branch.

- [Completed benchmark, implementation and limitations](OPTIMIZED.md).
- [Two-minute presentation](../../presentation-dma-vulkan-2026-11-04.md).
- [Chronological CPU-only thread benchmark on lecso](../onnx-threads-stages-20261103/README.md).

## Saved source and evidence

| Archive | Contents |
| --- | --- |
| `optimized-source-and-evidence.tar.xz` | Final runner, ORT and Dawn patches, build provenance, repeated timings, detection comparisons, hardware samples and profile |
| `source-and-evidence.tar.gz` | Original OPK experimental adapter, base helpers and pre-update checkpoint |
| `resumed-source-and-evidence.tar.gz` | Intermediate driver-update investigation and build checkpoint, before the initialization fix |

The original archives remain unchanged. Their dated reports describe historical states: [pre-update results](RESULTS.md), [investigation](INVESTIGATION.md), [research](RESEARCH.md) and [intermediate execution blocker](RESUMED.md). Use [OPTIMIZED.md](OPTIMIZED.md) for the current result. The standalone `mali-tiling.patch` is now the measured ORT patch; `dawn-zero-init.patch` is the separate dependency patch. Archived earlier versions retain their original provenance.

Each new archive contains a verified SHA-256 member manifest. Downloaded models, licensed video, compiled libraries, credentials and local plans are excluded. Reuse OPK's model and demo-media download owners when recovering assets. The scripts preserve the actual dated paths used during measurement.

```sh
sha256sum -c SHA256SUMS
mkdir original optimized
tar -xzf source-and-evidence.tar.gz -C original
tar -xJf optimized-source-and-evidence.tar.xz -C optimized
```

## Rebuild and recovery

The final source is based on ONNX Runtime commit `c44523c625e21f713df88e5462d09316298efe28`, provider tag `plugin-ep-webgpu/v0.5.0`, with Dawn `v20260916.214021`. Both CPU and GPU use ONNX Runtime 1.31.1. Follow the patch order and archived build settings described in [OPTIMIZED.md](OPTIMIZED.md). The Dawn dependency patch must survive dependency population.

The final local provider and evidence are under:

```text
/home/sanmeg01/.cache/opk-benchmarks/opi6-four-stage-20261102/webgpu-yolo26-20261103/fix-20261103/
```

The existing cross-build can be resumed with:

```sh
docker start opk-opi6-four-stage-build-20261102
docker exec opk-opi6-four-stage-build-20261102 bash -lc '
  source /opt/opk-venvs/devtools/bin/activate
  cd /bench/webgpu-yolo26-20261103/mali-tiling-20261103/onnxruntime
  python tools/ci_build/build.py @../build-options.txt --build
'
```

The final runner selects `/etc/vulkan/icd.d/mali.json` and checks physical Mali mappings. Do not restore archived driver libraries over the colleague's updated system.

Earlier runtime/media backups remain at `C:\Users\sanmeg01\OPK-backups\opi6-gpu-20261103\`; those binaries predate the final fix. The pushed source archives preserve the new implementation without depending on the OPi6 filesystem. No task-owned benchmark remains running on OPi6. Its CPU governors are restored to `schedutil` and the DMA heap to `0600 root:root`.

This is an experimental backend. Other GPUs, native macOS/Windows GPU execution and GPU execution inside WSL are untested.
