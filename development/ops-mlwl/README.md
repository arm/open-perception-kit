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

## CPU quickstart

On Linux x86-64 or AArch64, add these settings to the checkout's `.env` before
running the [quickstart](../../README.md#2-install-prerequisites-build-and-start-the-docker-container):

```dotenv
OPK_MLWL=enabled
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json
VK_INSTANCE_LAYERS=VK_LAYER_ML_Graph_Emulation:VK_LAYER_ML_Tensor_Emulation
```

The development image builds MLWL and Arm's
[Vulkan emulation layers](https://github.com/arm/ai-ml-emulation-layer-for-vulkan)
at [`67199c9f`](https://github.com/arm/ai-ml-emulation-layer-for-vulkan/commit/67199c9fae2cee900d41d659937870fb478e7de9)
and installs Mesa lavapipe. The Vulkan settings select CPU execution explicitly;
installing MLWL alone does not force a device or enable emulation layers.

After entering the container, build OPK and run the existing two-model demo:

```sh
./scripts/build.sh
./scripts/run.sh mlwl-vgf-demo
```

Supply the [UltraFace](../../config/models/ultraface-rfb-320-mlwl/index.md) and
[YOLO](../../config/models/yolo26n-320-mlwl/index.md) VGF files first; they are not
downloaded yet. Open `http://localhost:9999/` for the WebUI, or use the host's
address when browsing from another machine. Emulation runs on the CPU and is
intended for functional testing, not GPU performance measurements.

## Raspberry Pi 5 GPU quickstart

Use Docker Compose 2.24.4 or newer and a prepared OPK development image with
MLWL, `vulkaninfo`, a compatible V3D driver and Graph/Tensor support.
The image must select only the V3D GPU through
its Vulkan environment. Platform preparation belongs to the image or OS provider;
quickstart does not build or patch GPU drivers. Stock Raspberry Pi OS is not sufficient.
For an evaluation image, see the [RPi5 runtime build](../../scripts/private/rpi5-gpu/README.md).

Add these settings to the checkout's `.env`, using your prepared image tag:

```dotenv
OPK_DEV_IMAGE=your-rpi5-gpu-image:tag
OPK_MLWL_RUNTIME=rpi5-gpu
```

Run `./scripts/quick_start.sh`, then follow the normal build and
`./scripts/run.sh mlwl-vgf-demo` steps above. Quickstart uses the image without
rebuilding it and gives the container access to `/dev/dri/renderD128`.

The preflight checks device permissions, the MLWL library, V3D selection and all
extensions required by the pinned MLWL runtime: `VK_ARM_data_graph`,
`VK_ARM_tensors`, `VK_KHR_deferred_host_operations` and `VK_KHR_maintenance5`.
**NOK lists missing extensions and stops quickstart with a nonzero exit code.**
`run.sh` repeats the check before starting a pipeline; there is no CPU fallback.
Device features, model correctness and GPU speed are not checked by this preflight.
To check the preflight's success and failure paths in that container, run
`python3 development/tests/mlwl_gpu_preflight_test.py`.

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
