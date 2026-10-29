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

# Host-memory API conformance

These GTest tests exercise the public [runtime ABI](../loom_runtime.h) without
OPK runtime classes, models or plugins. The default provider is an in-process
mock network that doubles four Float32 values. Passing the mock demonstrates
that the suite works; it does not certify another runtime.

## Build and run independently

Requirements: C++17 compiler, Meson/Ninja and an existing GTest installation
providing `gtest_main` through pkg-config or CMake. Nothing is downloaded.

```sh
meson setup build development/ops-loom/interface/conformance
meson test -C build --print-errorlogs
```

The source directory can move with the API: keep it directly below the directory
containing `loom_runtime.h`. No other OPK files are needed. OPK's parent build can
also compile `host_conformance_test.cpp` and a provider against its existing
GTest dependency. The default standalone build runs the normal suite and two
expected-failure runs proving that wrong output and a clobbered bootstrap table
are detected. Their nonzero exits are expected; Meson reports success only when
the defective implementation is rejected.

## Test your implementation

Implement `lrt_conformance::make_provider()` from [provider.h](provider.h) in a
replacement source file. Keep runtime-specific setup out of the test file.

```sh
meson setup build-real development/ops-loom/interface/conformance \
  -Dprovider_sources=/absolute/path/to/my_provider.cpp
meson test -C build-real --print-errorlogs
```

The provider may call statically linked runtime code or load a shared library.
For a dynamic loader, keep the library loaded until process exit: failed
conformance must never unload code still used by a defective runtime. Supply
additional adapter/runtime sources through the `provider_sources` array, or
build the test file/provider with your own compiler/link settings if your runtime
needs extra libraries.

The provider supplies:

- `get_api` and the actual exported `lrt_runtime_version` value.
- Self-contained serialized model bytes. The suite overwrites/frees a temporary
  source copy after loading to check the source-borrow lifetime.
- An optional existing path for a PATH source of the same model. Empty paths
  explicitly skip the path test; memory loading remains mandatory for this profile.
- Complete input byte payloads and an output oracle that checks bytes using the
  runtime's model semantics and numerical tolerances. The test itself assumes no
  network or dtype. Padding/input initialization belongs in these fixture bytes.
- `input_option_indices`/`output_option_indices`: one known-compatible complete
  host POINTER configuration set from this model's `get_io_requirements` options.
  Different tensors may use different methods/configurations. The baseline must
  run; it is never silently skipped because all host paths are absent.
- Optional `arm_pending`, `finish_pending`, `fail_next_execution` controls. Omit
  unsupported controls; the corresponding deterministic timing/failure tests skip.
  These are fixture controls, not new runtime ABI operations.

The model fixture must have nonempty static tensors, non-data-dependent outputs,
at least one input/output, and deterministic output for the supplied input across
repeated execution. Stateful models are suitable only when the supplied fixture
has that behavior. The mock is stateless. Async-only runtimes are supported;
normal invocation waits have a finite two-second deadline.

## Coverage and exclusions

The suite checks exact-version bootstrap, all core entries, model/session/source
lifetime, host configurations, allocation/import/borrow methods, shapes/strides,
capacity/alignment, rejected binding atomicity, imported subrange offsets,
complete-set validation, repeated outputs, output metadata, stale completions,
parent/buffer busy lifetimes, context mismatch, completion failures and reset.

Per-tensor advertised methods need not form a compatible complete combination.
Optional homogeneous allocation/import/borrow tests skip an incompatible full
combination. They also skip when a method is not advertised for every fixture
tensor. Skips are printed by GTest and must be reviewed alongside pass counts.
The provider-selected baseline remains mandatory.

The mock's async gate is a deterministic simulation: the first poll remains
pending and a subsequent nonzero wait completes work. It does not test a real
worker thread or concurrent memory visibility. Failure injection reports one
accepted execution failure, followed by reset and recovery. `LRT_MOCK_FAULT`
selects intentionally broken mock behavior only for negative runner checks;
generic conformance tests never inspect that variable.

This first profile excludes Vulkan, DMA-BUF, all device-memory configurations,
dynamic shapes/data-dependent outputs, stress/concurrency and allocator/device
failure exhaustiveness. Passing means the exercised host profile passed, not
that every optional ABI feature conforms.

When a destructive call unexpectedly succeeds, or work cannot quiesce before
cleanup, the runner exits immediately with failure rather than calling a stale
handle or freeing in-flight host storage. Use a process timeout around external
runtimes; no test can recover safely from a runtime that blocks forever inside
an ABI call. Standalone Meson sets a 60-second process timeout.
