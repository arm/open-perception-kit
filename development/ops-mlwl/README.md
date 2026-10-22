# ML Workload Library inference

`opk-mlwl-ops/Inference` loads a VGF model using the public ML Workload Library
for Vulkan (MLWL). It implements OPK's existing `modelDescriptor` contract;
preprocessing and postprocessing stay in the usual OpChain. No device names,
board configuration, model conversion or alternative inference runtime are
embedded in the plugin.

## Build

The optional backend is disabled by default. It consumes the shared library from
[MLWL 0.11.0](https://github.com/arm/ai-ml-workload-library-for-vulkan/releases/tag/release/v0.11.0)
and Vulkan-Headers `vulkan-sdk-1.4.357.0`, matching that release's SDK manifest.
Install their checksum-locked CMake packages into a new dependency prefix inside
the OPK development container (Linux x86-64 or AArch64, Python 3.12+):

```sh
python3 scripts/private/install-mlwl.py /path/to/sdk
export MLWorkloadLibraryForVulkan_DIR=/path/to/sdk/lib64/cmake/MLWorkloadLibraryForVulkan
OPK_MLWL=enabled ./scripts/build.sh debug true \
  --extra-setup-args=-Dcmake_prefix_path=/path/to/sdk
meson test -C development/build-active --print-errorlogs
```

The MLWL-facing targets use `NDEBUG` even in a debug OPK build: Vulkan-Hpp's
dispatcher layout must match the published Release library and header version.
Do not mix a different MLWL binary/header build into this prefix. The shared
library must also be discoverable by the runtime loader when deploying the
plugin. The Vulkan loader, driver and optional emulation layers belong to the
execution environment, not OPK.

Release packaging does not yet stage MLWL's runtime and licences. A release
configuration fails if MLWL is enabled (including `auto` when dependencies are
found). Use `-Dmlwl=disabled` for releases or `-Drelease_package=false` for an
MLWL development build.

## Contract

- `modelFile` points to a VGF file, resolved normally from its model descriptor.
- Public inputs and outputs are contiguous, non-aliased Float16/Float32 tensors.
  Descriptor order, shapes and types must match the workload's input/output
  order. Unsupported resources fail during configuration.
- Dynamic outputs, tensor feedback and unbound placeholder modules are not
  supported. Model-specific decoding belongs in a downstream postprocessor.
- Graph-only workloads use an OPK-owned Vulkan context with a DataGraph queue,
  tensors, `shaderTensorAccess`, graph-module support, `shaderInt8`,
  `vulkanMemoryModel` and `synchronization2`. These features are queried before
  being enabled; a device missing any of them is skipped. Compute is not required.
  Workloads containing compute executables use MLWL's default context requirements.
  Selection is capability-based; there is no named-device filter or fallback to
  another OPK backend. MLWL owns session configuration and execution in both cases.
- Sessions, bindings and coherent tensor mappings are reused. Output views stay
  valid until the next execution or teardown, as with other OPK backends.
- Setup and execution errors propagate through OPK's normal error results.
  A failed reconfiguration does not leave the previous model runnable.

MLWL still labels its C++ API experimental. Execution is synchronous, like the
current OPK OpChain. MLWL 0.11.0 has no cancellation/finite-timeout API: normal
teardown follows completed execution; a hung driver is not made cancellable by
this plugin. Do not destroy in-flight Vulkan resources to simulate cancellation.

## Validation

The normal Meson tests cover tensor layout bounds and real GStreamer error
propagation without a Vulkan device. Successful inference is a separate,
opt-in integration check, not a mocked device test.
The required ARM64 Meson CI job enables MLWL and runs these device-free checks.

With a compatible Vulkan runtime, the public
[model converter 0.11.0](https://github.com/arm/ai-ml-sdk-model-converter/releases/tag/release/v0.11.0),
and a Python interpreter with GStreamer GI and FlatBuffers installed, run from
the repository root:

```sh
PYTHONPATH=generated/open_perception_kit/python/src \
  python3 development/tests/mlwl_pipeline_test.py \
  development/build-active/meson-out /path/to/model-converter
```

It compiles a tiny FP32 add graph into a temporary VGF and checks actual
FrameResults from image preprocessing → MLWL → segmentation postprocessing →
`opkcomm`. Alternating black/white frames and a nonzero scalar input cover buffer reuse,
descriptor constants and startup/teardown across three pipelines. Each emits
two frames whose output values are checked through the generated SDK.
The VGF is generated locally; no model binary is checked in. This checks backend
integration, not a full neural network, hardware performance or FP16 execution.
