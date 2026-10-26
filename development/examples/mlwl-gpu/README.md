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

# MLWL GPU examples

## Raspberry Pi 5

In the existing AArch64 OPK Docker development container on RPi5, first copy the
[OPK-adapted UltraFace VGF](../../../config/models/ultraface-rfb-320-mlwl/index.md)
to `config/models/ultraface-rfb-320-mlwl/ultraface-rfb-320.vgf`. The VGF is supplied
manually until it is available through the model's Hugging Face download entry.
The container must expose `/dev/dri/renderD128`, as the standard OPK Compose
environment does. Then, from the OPK root:

```sh
python3 development/examples/mlwl-gpu/rpi5.py
```

The command checks the model checksum, installs build packages inside the
container, builds MLWL, a private Mesa 26.2.1 driver and Arm's graph/tensor
layers, then runs UltraFace on the bundled image. It uses the repository's
Meson targets for `opchain-exec` and the two required Op plugins. Internet is
needed on the first run. Passwordless sudo is used for package installation
and whenever the container needs the host render-group ID.
Sources, upstream notices and build outputs stay in `.cache/mlwl-rpi5`;
later runs reuse them. Remove that cache when changing the container image.

The driver enables deferred host operations and lowers Int64 to 32-bit GPU
instructions. The layers use `VMEL_USE_FLOAT_AS_DOUBLE=ON` because their Clamp
shader otherwise requires Float64 even with this FP32 model. These are
experimental driver changes, without Vulkan CTS validation. System drivers
are unchanged. The command checks for the physical V3D GPU and never falls
back to CPU. Successful inference prints FrameResults with seven faces.
In a render-only container, Mesa may log `Opening ... failed` while probing
the absent primary DRM node; compute inference still works.

## Orange Pi 6

Build OPK with [MLWL enabled](../../ops-mlwl/README.md#build) on AArch64 and supply the same VGF. For Mali-G720 MC10, use
a private Mesa 26.0.5 PanVK driver with `KHR_deferred_host_operations` enabled
in its physical-device extension table. Native Int64 already works.

Build [Arm's emulation layers v0.11.0](https://github.com/arm/ai-ml-emulation-layer-for-vulkan/tree/v0.11.0)
with `VMEL_USE_FLOAT_AS_DOUBLE=ON` and supply these files:

```text
gpu-runtime/
  panfrost_icd.json
  lib/                              # driver and its dependencies
  emulation-float/lib/               # libVkLayer_Graph.so, libVkLayer_Tensor.so
  emulation-float/share/vulkan/explicit_layer.d/
    VkLayer_Graph.json
    VkLayer_Tensor.json
```

Each manifest must resolve its library at the deployed path. Keep upstream
runtime notices. Make `vulkaninfo` available on `PATH` and grant access to
`/dev/dri/renderD128` (pass the device and runtime directory into containers).
The stock Orange Pi Vulkan image alone lacks the Arm layers and OPK runtime.

From the OPK root, set the runtime and SDK paths below.
The native-device check stops before inference on a CPU, a
wrong vendor, multiple devices, or a failed probe.

```sh
bash development/examples/mlwl-gpu/orangepi6.sh /absolute/gpu-runtime /absolute/mlwl-sdk
```

On 2026-10-26 both boards produced seven faces and matching FrameResults using
MLWL `03c9aa11` and the same OPK build from `ca5b53d2` (PR181). This validates
single-image GPU inference; camera/WebRTC and the combined YOLO demo were not tested.
The RPi5 command above was also validated with source-built dependencies and
a repeat run using the cache; both produced the same seven faces and FrameResults.
