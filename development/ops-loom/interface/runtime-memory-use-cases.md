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

# Tensor memory and reuse

This guide describes the [consumer ABI](loom_runtime.h), not device support.
Inputs and outputs select storage independently. Every selection must satisfy
its tensor requirements and pass complete-set binding validation.

## Choose a binding path

| Application storage | Operations | Lifetime |
| --- | --- | --- |
| Fixed CPU tensor allocation | `bind_host_tensor`, validate once, refill/run repeatedly | Keep storage alive while bound |
| Changing CPU tensor pointer | Bind, validate, run, complete, unbind | Keep owner and mapping alive through unbinding |
| External resource or pool | Import stable allocations once; bind views/slots and validate after each rebind | Keep native owners alive until imports are released |
| Runtime-owned storage | Allocate selected configuration, query buffer info, bind and validate | Unbind/destroy session before releasing owned handles |

Bindings select exact owner, accessibility, reference and DIRECT/STAGED transfer.
Accessibility is a minimum guarantee for the same allocation, not physical
placement or permission for simultaneous access. Unsupported selections fail
without an automatic copy or replacement configuration.

## Representable memory categories

The case labels identify analogous input/output mechanisms, not required pairs.
They do not promise that a backend supports every row or combination.

| Input / output | Owner | Access / reference | Contract path |
| --- | --- | --- | --- |
| I1 / O1 | Runtime | Host / pointer | Allocate and bind runtime storage |
| I2 / O2 | External | Host / pointer | Borrow pointer or import reusable storage |
| I3 / O3 | Runtime | Runtime device / native resource | Optional Vulkan allocation profile |
| I4 / O4 | External | Runtime device / native resource | Import compatible resource on the advertised device/queue |
| I5 / O5 | Runtime | Host and runtime device / shared allocation | Advertised pointer path or Vulkan buffer with CPU-map capability |
| I6 / O6 | External | Host and runtime device / shared allocation | Compatible pointer binding or native import and CPU mapping |
| I7 / O7 | External | DMA-BUF / accessibility depends on import | Import tensor byte range; optional fences and CPU-map extension |
| I8 / O8 | External | Other device / native resource | Needs a separately supported sharing profile or an application transfer |

DMA-BUF identifies shared storage. It does not identify image interpretation or
establish readiness. Camera planes, offsets, pixel format and DRM modifiers
require a separate image/preprocessing contract. The core imports tensor bytes;
the Vulkan image profile is limited to the representation in its companion.

## Output handoff

| Mode | Reuse rule |
| --- | --- |
| H1 borrowed runtime output | Consume before the next accepted invocation, binding change or session destruction |
| H2 retained runtime output | Reserved; no output lease exists in this draft |
| H3 external output storage | Caller controls pool reuse after runtime and downstream consumers finish |

An idle session can bind a different external output slot for its next run.
Copy required result metadata before rebinding, since rebinding invalidates
`get_outputs` views. Keeping a completion or buffer handle is not an H2 lease.
Do not release borrowed buffer handles returned in results.

## Synchronization

Complete external producer writes and prior consumer reads before core inference.
Where supported, the Linux extension accepts producer sync-file fences for
asynchronous inference instead. A completed invocation establishes production and
promised visibility; it does not establish downstream consumer completion.

A timed-out wait leaves work pending. Keep bindings, mappings and pool slots
alive. Terminal failure also guarantees no further runtime IO access, but outputs
are invalid and network state must be reset before reuse.

DMA-BUF/Vulkan CPU access must be bracketed by the advertised map/access calls.
Coherent memory removes cache-maintenance requirements, not execution or memory
visibility dependencies. Vulkan native access follows the device/queue handoff
rules in [loom_runtime_vulkan.h](loom_runtime_vulkan.h).

When integrating GStreamer, retain the owning buffer/pool slot and mapping;
retaining an FD alone does not prevent the producer overwriting a slot. Obtain
final writable storage before binding: mapping or writability operations may
replace/copy storage. Image-to-tensor conversion remains explicit.

## Current limits

All options for a tensor share one required stride layout. Duplicate selection
keys with different constraints are not representable: advertise conservative
common constraints. Complete-set validation detects incompatible combinations,
but there is no combination graph, atomic batch binding or rollback. Measure
binding/setup cost when resources change per frame.
