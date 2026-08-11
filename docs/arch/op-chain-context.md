---
sidebar_position: 13
sidebar_label: OpChain Context
---

# OpChainContext

`OpChainContext` stores transient data for one OpChain execution. Data in the
context is discarded when the chain completes. Anything that must survive
downstream must be copied into `Perception`.

## Lifetime Rules

- Context data is valid only for the current OpChain execution.
- Pointers stored in the context must reference buffers that remain valid for the
  whole execution.
- Producers own referenced memory and are responsible for lifetime management.
- Persistent results belong in `Perception`, not in the context.

## Named Bitmap Views

`bitmapViews` maps names to `pek::BitmapView` instances. The primary input view is
usually `"pipelineVideoFrame"`, which represents the GStreamer video frame made
available to Ops.

Ops can query named views to access input or intermediate image data without
copying frame memory.

## Loop Control

`loopId` on each Op identifies a contiguous repeated section. The OpChain
executor runs the group's first Op once, then repeats its remaining Ops.

`OpSignal::BreakLoop` lets an Op stop the active loop early. `GenericImagePreprocess`,
for example, returns it when there are no more crops to process.

## Inference Crops

`inferenceImageCrops` stores regions scheduled for inference.
`inferenceImageCropUuids` stores the matching parent object UUIDs. Multi-crop
flows consume one crop per loop iteration until the list is empty.

## Perception Pointer

`perception` points to the persistent metadata object for the current buffer. Ops
write durable outputs there, including detections, layer metadata, and other
results that downstream elements need.
