---
sidebar_position: 13
sidebar_label: OpChain Context
---
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


# OpChainContext

`OpChainContext` stores transient data for one OpChain execution. Data in the
context is discarded when the chain completes. Anything that must survive
downstream must be appended to the `FrameResults` instance referenced by
`frameResults`.

## Lifetime Rules

- Context data is valid only for the current OpChain execution.
- Pointers stored in the context must reference buffers that remain valid for the
  whole execution.
- Producers own referenced memory and are responsible for lifetime management.
- Persistent results belong in typed `FrameResults` payloads, not in context-owned
  temporary state.

## Named Bitmap Views

`bitmapViews` maps names to `opk::BitmapView` instances. The primary input view is
usually `"pipelineVideoFrame"`, which represents the GStreamer video frame made
available to Ops.

Ops can query named views to access input or intermediate image data without
copying frame memory.

## Prompt Inputs

`prompt` is the optional immutable `PromptSnapshot` paired with the invocation's
media. It owns the session, generated request envelope and a polymorphic semantic
payload. The context has no box-, text- or model-specific prompt fields. Payload
format/version and destination support are delegated to `OpInterfacePromptConsumer`.

`preparedPrompt` records completed prompt input preparation for the next matching
inference. Only a preprocessor that successfully wrote those inputs sets it.
`OpChain` consumes that preparation at the actual inference boundary and owns
`inferencePrompt`, the association for successfully produced output tensors.
A new inference replaces the earlier output association, including when its
preprocessing used OCR rather than the generic image Op. Chain entry, error,
abort and skipped-inference paths must not retain stale preparation/association.
`GenericPostprocess` passes only the output-associated snapshot to the parser,
which explicitly associates its own produced results with that request.
The generated request envelope is encapsulated; public accessors expose only
read-only identity/destination values and serialization uses a detached copy.
See the [prompt runtime contract](promptable-inputs-proposal.md#runtime-snapshot-and-extension-boundary).

## Loop Control

`loopId` on each Op identifies a contiguous repeated section. The OpChain
executor runs the group's first Op once, then repeats its remaining Ops.

`OpSignal::BreakLoop` lets an Op stop the active loop early. `GenericImagePreprocess`,
for example, returns it when there are no more crops to process.

## Inference Crops

`inferenceImageCrops` stores regions scheduled for inference.
`inferenceImageCropUuids` stores the matching parent object UUIDs. Multi-crop
flows consume one crop per loop iteration until the list is empty.

## FrameResults Pointer

`frameResults` points to the persistent metadata object for the current buffer. Ops
write durable outputs there, including detections, layer metadata, and other
results that downstream elements need.
