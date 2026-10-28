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
(Linux x86-64 or AArch64, Python 3.12+), build MLWL at
[`03c9aa11`](https://github.com/arm/ai-ml-workload-library-for-vulkan/commit/03c9aa11efa6d8dd65ae5f6097bf86a00c304ad9)
and install it with matching Vulkan headers into a new directory:

```sh
python3 scripts/private/install-mlwl.py /path/to/sdk
export MLWorkloadLibraryForVulkan_DIR=/path/to/sdk/lib64/cmake/MLWorkloadLibraryForVulkan
OPK_MLWL=enabled ./scripts/build.sh debug true \
  --extra-setup-args=-Dcmake_prefix_path=/path/to/sdk
meson test -C development/build-active --print-errorlogs
```

The installer uses checksum-pinned sources from `requirements/mlwl.json` and
builds them in Release mode. CMake, Ninja and a C++17 compiler are required.
This is a development snapshot, not an MLWL release.

Keep the installed library and headers together. The build sets `NDEBUG` for
MLWL targets to match the library's Vulkan-Hpp ABI, including in debug builds.
At runtime, the loader must find the MLWL shared library and a compatible Vulkan driver.

MLWL release packaging is not supported yet. Use `-Dmlwl=disabled` for release
packages or `-Drelease_package=false` for MLWL development builds.

## Models

- Set `modelFile` to the VGF path, relative to the model descriptor.
- Inputs and outputs must be contiguous, non-aliased Float16/Float32 tensors.
  The descriptor's tensor order, shapes and types must match the workload.
- Dynamic outputs, tensor feedback and unbound placeholder modules are unsupported.
- MLWL creates the Vulkan context. Graph workloads request DataGraph shader
  modules and tensors; other workloads use MLWL's default device requirements.
  There is no fallback to another backend.

MLWL's C++ API is experimental. Execution is synchronous; this revision has no
cancellation or timeout API.

## Tests

Meson tests check tensor layout bounds and invalid-VGF startup without a Vulkan
device. The ARM64 CI job enables MLWL and runs these checks.

For inference testing, use a compatible Vulkan runtime,
[model converter 0.11.0](https://github.com/arm/ai-ml-sdk-model-converter/releases/tag/release/v0.11.0),
and Python with GStreamer GI and FlatBuffers. Build OPK with MLWL and
Python Ops enabled; the test uses PythonScript to capture raw output tensors:

```sh
PYTHONPATH=generated/open_perception_kit/python/src \
  python3 development/tests/mlwl_pipeline_test.py \
  development/build-active/meson-out /path/to/model-converter
```

The test converts a small FP32 add graph to VGF and checks raw FP32 outputs
against reference values with an absolute tolerance of `1e-6`, alongside the
segmentation FrameResults. It covers image and scalar inputs, buffer reuse,
and pipeline startup and teardown.
