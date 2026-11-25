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

# Native visual prompt adapters

The visual prompt extension surface separates common request/preparation lifetime
from the model export's semantics. It currently serves the MobileSAM box export
and YOLO-E text/visual conditioning. It does not change either model's image or
postprocessing numerics, public wire formats or cancellation rules.

## Compose a native adapter

Use [PromptAdapter.h](../../development/common/op/PromptAdapter.h) through the
normal native Op plugin factory. A concrete Op constructor supplies its native
adapter to one of these shared wrappers:

| Wrapper | Adapter responsibility | Shared responsibility |
| --- | --- | --- |
| `PromptInputOp` | `PromptInputAdapter::writeInputs()` validates current geometry and writes all declared prompt tensor inputs. | Validate snapshot/accepted format, clear stale preparation, publish `preparedPrompt` only after successful writes for the same snapshot. |
| `PromptPrepareOp` | `PromptPreparationAdapter::prepare()` derives immutable revision-dependent state, optionally using learned inference. | Exact snapshot/contract matching, synchronous one-entry cache, retained-owner accounting and atomic publication. |

Both wrappers currently skip the chain for an absent payload. This is the policy
supported by the two visual integrations, not a universal model default. Malformed
present requests fail. Input adapters never assign `preparedPrompt` or
`inferencePrompt`; the wrapper and OpChain runner own those markers respectively.

MobileSAM only uses `PromptInputOp` with `MobileSamBoxAdapter`: there is no encoder,
prepared-state allocation or revision cache. Its public Op remains
`opk-std-ops/BoxPromptPreprocess`.

YOLO-E uses `YoloePromptPrepareOp` with `YoloePreparationAdapter`,
`YoloePromptPackOp` with `YoloePromptInputAdapter`, and
`YoloeInstancePostprocessOp`. These registrations replace the prototype's generic
`Query*` names. The adapter owns tokenization/reference algorithms and its ONNX
encoder session. Encoder and detector reuse the existing shared ONNX backend;
this does not introduce a new graph scheduler.

## Prepared state and compatibility

Derive an immutable state from `PreparedPrompt`. It carries its exact originating
snapshot, a nonempty versioned `PreparationContract`, and `ownedBytes()` for
additional owned allocations. Contract identifiers have static storage duration.
No shared contract assumes Float32, a single tensor, a fixed feature width or group
count. `YoloePreparedPrompt` supplies YOLO-E features and group ordering; a native
adapter can instead own Int64 tokens, masks or other representations. Reuse
`TensorView` when a tensor view is needed; retain its backing owner.

The common provider returns state only for the requested snapshot and contract.
Consumers must also validate the concrete representation and identity. Equal
tensor shapes do not establish compatible embedding spaces. Model generation is
scoped to the configured adapter: reconfiguration drops the current cache and
preparation uses the newly configured assets.

`maxPreparedBytes()` bounds each prepared value. At most
`PromptController::MaxRetainedRevisions` prepared values, with that multiple of the
per-value byte bound, may remain owned through this cache's borrowers. Semantic
snapshot storage is accounted separately by the existing controller. Old borrowed
states remain valid through replacement, absence and reconfiguration, and still
count against retention limits while alive. A failed preparation publishes no
new cache entry and cannot fall back to earlier conditioning. Evicted state can
be rebuilt from its owned semantic snapshot.

Preparation is synchronous and only appropriate for work determined by the
snapshot and configured adapter. Target-frame-dependent work belongs in invocation
binding. Do not cache target geometry or writable backend pointers across frames.

## Binding and capability discovery

`OpInterfaceInferenceInputPreprocessor` declares conditioning tensor ownership.
`OpInterfaceImageInputPreprocessor` separately identifies an image writer that
publishes the actual source/crop/resize/padding in `inferenceInfo.image`. A plain
conditioning writer cannot substitute for that image capability. Adapters validate
the configured image contract and the current invocation's geometry before writing.
Resizing and shared-storage support remain optional backend capabilities; MobileSAM
does not acquire a dynamic-input requirement.

`OpInterfacePromptConsumer::promptCapabilities()` advertises configured adapter
restrictions, and `OpChain::promptCapabilities(inferenceInstance, sourceName)`
selects matching destination capabilities. Capability spans borrow adapter storage
until reconfiguration/destruction; identifier strings have static storage duration.
An empty list means detailed restrictions are not advertised. Zero numeric fields
mean unspecified, not unlimited. `acceptsPrompt()` remains the authoritative bound
format/destination check. This native discovery API does not replace `opkprompt`'s
`capabilities` property, which describes ingress decoders only.

Both capability types compose `PromptLimits` for their shared semantic bounds
(groups, references, reference dimensions/bytes and description bytes). Each owns
its own values: decoding support does not imply model support. Serialized-size and
group-name limits remain codec-specific; contract identity and token limits remain
consumer-specific. The `opkprompt` capability JSON keeps its existing flat fields.

## Future Python BYOM boundary

A future trusted model-local Python adapter can implement the same revision and
invocation semantics behind these native wrappers. Its prepared data must be owned,
immutable and byte-accounted; returned tensors must be checked before copying into
OPK-managed inputs. It must not control ingress state, raw backend pointers or the
inference-consumption marker. Learned inference should reuse OPK-managed runtime
facilities rather than create independent initialization policies inside scripts.

No Python adapter loader, writable input bridge, script callback API or new Python
BYOM example is implemented here. The existing Python postprocessing API remains
unchanged. Audio, asynchronous preparation and autoregressive scheduling are outside
this implementation.
