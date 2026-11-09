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

# OR YOLO26 prototype

`opk-or-ops/Inference` loads a trusted native runtime library selected by
`runtimeLibrary` in `model.json`. `modelFile` selects the network, independently.
Both paths resolve relative to the descriptor; absolute paths are preserved.
Model descriptor version 1.3 adds this optional field. Existing ONNX, ExecuTorch
and MLWL providers keep their own model loading behavior.

The checked-in descriptor uses the YOLO26n runtime in OPK's pinned
[Optimized-Runtimes submodule](../../third-party/optimized-runtimes/yolo26-raspberry-pi-5/README.md).
From the OPK repository root, initialize it and build the matching OR library
and 320-resolution model package on its supported AArch64 Linux target:

```sh
git submodule update --init --recursive
cd development/third-party/optimized-runtimes/yolo26-raspberry-pi-5
make runtime
make model RESOLUTION=320
```

Follow the upstream [build prerequisites](../../third-party/optimized-runtimes/yolo26-raspberry-pi-5/README.md#acquiring-a-runtime)
for the compiler, system packages and model-conversion environment. The upstream
Makefile downloads and verifies its pinned checkpoint when `CHECKPOINT` is omitted;
use `make model CHECKPOINT=/path/to/yolo26n.pt RESOLUTION=320` for your own checkpoint.

The descriptor resolves these paths inside that submodule:

- `dist/models/yolo26n-320/model.bin`
- `dist/runtime/lib/libarm-yolo26n.so.0`

The built library exports OR directly; no separate adapter library is needed.
To use artifacts elsewhere, update both descriptor paths to your trusted local
files. Building OPK itself does not build the YOLO runtime or convert the model.

Use this directory's `opchain.json` from a normal OPK video pipeline. Its existing
preprocessor creates RGB FP32 NCHW data with centered 114/255 padding; the existing
YoloParser handles the dense center/size plus class-score output and applies NMS.
Change both descriptor shapes for another model resolution/class count.
No model or runtime binary is downloaded automatically by this example.

The provider is enabled by default (`-Dor_runtime=false` disables it). It negotiates
runtime-owned, host-accessible, direct pointer allocations for each tensor,
binds once and validates the full binding set. OPK preprocessing fills the
runtime input storage. Each frame calls synchronous inference and checks its
terminal result before exposing the fixed output buffers to postprocessing.
The library remains loaded until all runtime resources are released.

The OR (Optimized Runtime) interface comes from the pinned
[`arm/Optimized-Runtimes`](../../third-party/optimized-runtimes/interfaces/or/README.md)
submodule, tracking `feature/common-interface`. Initialize it as shown above
before configuring Meson or building an OPK container.

The provider and runtime must agree on the exact interface version (currently 10).
The plugin resolves `or_get_api`; rebuild libraries previously exporting
`lrt_get_api`. OpChains select `opk-or-ops/Inference`, and the Meson option is
`or_runtime`.

Prototype limits: static Float32 tensors, rank 1–8, stateless networks, synchronous
inference, runtime-owned direct host allocations, no tensor feedback. Unsupported
choices fail during configuration rather than silently changing memory paths.
The runtime library is executable code and must be trusted, just like an OPK plugin.

Build with OPK's existing arm64 Meson cross file / deployment Docker workflow;
`development/ops-or/opk-or-ops.so` is built with the other inference providers.
This OR integration was verified with an AArch64 cross-build and single-image
OPK inference at resolution 320 under QEMU user-mode emulation. Other resolutions
were not rerun, and physical Pi hardware and performance were not tested.
