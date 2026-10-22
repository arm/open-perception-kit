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

# MLWL inference

`opk-mlwl-ops/Inference` runs VGF models through the ML Workload Library for
Vulkan. Use `modelDescriptor` as with the other backends; preprocessing and
postprocessing stay in the OpChain.

## Build

The backend is disabled by default. In the OPK development container
(Linux x86-64 or AArch64, Python 3.12+), install the pinned MLWL 0.11.0 and
Vulkan headers into a new directory:

```sh
python3 scripts/private/install-mlwl.py /path/to/sdk
export MLWorkloadLibraryForVulkan_DIR=/path/to/sdk/lib64/cmake/MLWorkloadLibraryForVulkan
OPK_MLWL=enabled ./scripts/build.sh debug true \
  --extra-setup-args=-Dcmake_prefix_path=/path/to/sdk
meson test -C development/build-active --print-errorlogs
```

Keep the installed library and headers together. The build sets `NDEBUG` for
MLWL targets to match the release library's Vulkan-Hpp ABI, including in debug builds.
At runtime, the loader must find the MLWL shared library and a compatible Vulkan driver.

Release archive and deployment-image builds accept `--build-arg OPK_MLWL=enabled`.
They include the MLWL shared runtime and original dependency notices; archives
also carry its checksum receipt. Builds are native Linux x86-64 or AArch64,
and MLWL remains disabled by default. Supply the Vulkan driver and any emulation
layers in the execution environment.
The [Debian package target](../../docs/public/how-to/build-debian-runtime-package.md)
also accepts `-Dmlwl=enabled` with the same dependency prefix.

## Models

- Set `modelFile` to the VGF path, relative to the model descriptor.
- Inputs and outputs must be contiguous, non-aliased Float16/Float32 tensors.
  The descriptor's tensor order, shapes and types must match the workload.
- Dynamic outputs, tensor feedback and unbound placeholder modules are unsupported.
- Graph workloads need Vulkan DataGraph and tensor support. Compute workloads
  use MLWL's default device requirements. There is no fallback to another backend.

MLWL's C++ API is experimental. Execution is synchronous; MLWL 0.11.0 has no
cancellation or timeout API.

## Tests

Meson tests check tensor layout bounds without a Vulkan device.
