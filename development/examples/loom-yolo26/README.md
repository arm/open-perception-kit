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

# LOOM YOLO26 prototype

`opk-loom-ops/Inference` loads a trusted native runtime library selected by
`runtimeLibrary` in `model.json`. `modelFile` selects the network, independently.
Both paths resolve relative to the descriptor; absolute paths are preserved.
Model descriptor version 1.2 adds this optional field. Existing ONNX, ExecuTorch
and MLWL providers keep their own model loading behavior.

The checked-in descriptor uses this workspace's sibling `0/` checkout:

- `0/yolo26-raspberry-pi-5/dist/models/yolo26n-320/model.bin`
- `0/yolo26-raspberry-pi-5/dist/runtime/lib/libarm-yolo26n.so.0`

Build these using that runtime's `make model RESOLUTION=320` and `make runtime`
targets. The current library exports LOOM directly; no separate adapter library
is needed. Paths are relative to `model.json`. For a standalone OPK checkout,
set both paths to your trusted local artifacts. See the workspace
[`TUTORIAL.md`](../../../../TUTORIAL.md) for setup and verification status.

Use this directory's `opchain.json` from a normal OPK video pipeline. Its existing
preprocessor creates RGB FP32 NCHW data with centered 114/255 padding; the existing
YoloParser handles the dense center/size plus class-score output and applies NMS.
Change both descriptor shapes for another model resolution/class count.
No model or runtime binary is downloaded automatically by this example.

The provider is enabled by default (`-Dloom=false` disables it). It negotiates
runtime-owned, host-accessible, direct pointer allocations for each tensor,
binds once and validates the full binding set. OPK preprocessing fills the
runtime input storage. Each frame calls synchronous inference and checks its
terminal result before exposing the fixed output buffers to postprocessing.
The library remains loaded until all runtime resources are released.

The [runtime interface](../../ops-loom/interface/README.md) is maintained in
`ops-loom/interface`. The provider and native adapter must agree on its exact
contract version (currently 10).

Prototype limits: static Float32 tensors, rank 1–8, stateless networks, synchronous
inference, runtime-owned direct host allocations, no tensor feedback. Unsupported
choices fail during configuration rather than silently changing memory paths.
The runtime library is executable code and must be trusted, just like an OPK plugin.

Build with OPK's existing arm64 Meson cross file / deployment Docker workflow;
`development/ops-loom/opk-loom-ops.so` is built with the other inference providers.
The current runtime was exercised through OPK in the workspace's ARM64 QEMU
VM at every supported resolution. For that VM's 9p share, use the tutorial's generated configurations, which resolve the installed library symlink on
the host. Physical Pi hardware and performance were not tested in this run.
