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

# Runtime consumer interface

Draft C ABI, contract version **10**, maintained with OPK. This is a local
application/runtime contract; it is not an upstream LOOM API. A native network
runtime may implement it directly or through an adapter. Existing ONNX,
ExecuTorch and MLWL integrations keep their own backend interfaces.

| File | Purpose |
| --- | --- |
| [loom_runtime.h](loom_runtime.h) | Authoritative core types, lifecycle, memory binding and execution rules |
| [loom_runtime_vulkan.h](loom_runtime_vulkan.h) | Optional typed Vulkan buffer/image profile and handoff rules |
| [Contract](loom-runtime-contract-proposal.md) | Responsibilities, lifecycle and execution semantics |
| [Memory guide](runtime-memory-use-cases.md) | Binding choices, output reuse and synchronization |
| [Declaration checks](tests/) | Standalone C11/C++11 compatibility and include-order checks |
| [Host conformance suite](conformance/README.md) | Replaceable GTest runtime provider and host-memory contract tests |

## Consumer lifecycle

1. Load the trusted runtime library and resolve `lrt_get_api`. Request exactly
   `LRT_RUNTIME_VERSION`; reject failure before reading the function table.
   Verify `api.get_runtime_version()` matches. Keep the library loaded until
   every runtime object is released. The runtime also exports the immutable
   `lrt_runtime_version` value for optional pre-bootstrap inspection.
2. Create a context, query capabilities, load a path or self-contained memory
   package, and describe its tensors. Package format is runtime-specific.
3. Create a session. Pass NULL options for static input shapes; resolve dynamic
   inputs through indexed shape overrides. Query resolved IO requirements.
4. Select an advertised memory configuration for every input and output.
   Borrow host storage, import external resources, or allocate runtime storage.
   Bind every tensor, then call `validate_bindings`.
5. Fill inputs and establish external readiness. Call `infer` for blocking
   execution or, if supported, `infer_async` followed by `wait`. Check execution
   success before consuming outputs. Release each terminal completion.
6. Refill and run repeatedly while bindings remain valid. Rebinding requires
   validation again. Do not overwrite outputs still used downstream.
7. Once idle, release completions, destroy sessions, release owned buffer handles,
   unload models and destroy contexts. Then unload the runtime library.

The runtime returns `LRT_UNSUPPORTED` for unavailable operations. Core function
pointers remain nonnull. Rejected submission accepts no work; accepted execution
reports failure through its completion. Timeout does not cancel work.

## Memory and output rules

Choose storage independently per tensor. `DIRECT` prohibits boundary staging;
`STAGED` explicitly requests an advertised staging path. Neither permits silent
fallback. Shape, dtype, strides, capacity, alignment and resource compatibility
must match. IO aliasing is unsupported.

Host pointers and native owners/mappings remain valid while bound/imported.
Imports do not retain external pool slots. Completion establishes production,
not downstream release. Output metadata is borrowed until the next accepted
invocation, binding change or session destruction. Retained output leases are
not supported. See the memory guide for pool-slot and native-resource reuse.

DMA-BUF and Vulkan are optional profiles, not zero-copy guarantees. The Vulkan
companion requires compatible device/queue ownership and explicit synchronization;
including the header does not enable those capabilities in a backend.

## OPK integration and status

The current `ops-loom` provider uses a restricted v10 subset: static, stateless
Float32 networks, synchronous execution and runtime-owned direct host allocations.
The broader declarations do not establish OPK support for dynamic outputs,
asynchronous execution, DMA-BUF or Vulkan imports. Preprocessing and semantic
output decoding remain in OPK unless explicitly included in the model package.

The [YOLO26 example](../../examples/loom-yolo26/README.md) describes a compatible
host adapter. Declaration checks below exercise neither that implementation nor
hardware behavior.

## Host conformance tests

The [conformance suite](conformance/README.md) checks runtime behavior through
the public API, using a mock host runtime by default. Implementors replace its
provider adapter with their API and known model fixtures. The suite needs only
the interface headers, a C++ compiler and GTest; it has no OPK runtime dependencies.

In an OPK test-enabled build:

```sh
meson test -C development/build-active loom_host_conformance_tests --print-errorlogs
```

See the suite README for standalone builds, provider requirements and coverage
limits. Passing the mock tests checks the harness, not a concrete runtime.

## Declaration checks

Run from this directory; no runtime library is linked:

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic -I . tests/header_contract.c -o /tmp/lrt-header-c
/tmp/lrt-header-c
c++ -x c++ -std=c++11 -Wall -Wextra -Werror -pedantic -I . tests/header_contract.c -o /tmp/lrt-header-cpp
/tmp/lrt-header-cpp
```

With Vulkan-Headers installed, add its include path as needed. Run the following
both with and without `-DLRT_TEST_CORE_FIRST` to cover both include orders:

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic -I . tests/vulkan_header_contract.c -o /tmp/lrt-vulkan-c
/tmp/lrt-vulkan-c
c++ -x c++ -std=c++11 -Wall -Wextra -Werror -pedantic -I . tests/vulkan_header_contract.c -o /tmp/lrt-vulkan-cpp
/tmp/lrt-vulkan-cpp
```

Before relying on a runtime/profile, validate known outputs, repeated execution,
binding replacement, invalid bounds, execution failures and owner lifetime.
Native-memory paths also need target testing of import, synchronization and reuse.
