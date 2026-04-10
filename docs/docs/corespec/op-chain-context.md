---
sidebar_position: 14
sidebar_label: OpChain Context
---

# OpChainContext
## Transient Execution Context for an OpChain Element

OpChainContext stores all transient data generated during the execution of an OpChain element.
All data contained in this context is temporary and is discarded when the OpChain completes.
Operations that require results to persist beyond the lifetime of the chain must explicitly copy
those results into the PerceptionContext.

### Lifetime Rules

- All data in this context exists only for the duration of a single OpChain execution.
- No data is automatically persisted beyond the end of the chain.
- Pointers stored in this context must reference buffers that remain valid throughout the entire
OpChain execution.
- Ownership and lifetime management of referenced buffers are the responsibility of the producer.

---

## Named Bitmap Views

`std::map<std::string, amp::BitmapView> bitmapViews;`

Named bitmap views provide shared image buffers accessible by Ops during execution.
The most important entry is `"pipelineVideoFrame"`, which represents the video frame
received from GStreamer and serves as the primary image source for processing.
Ops may query these views by name to access intermediate or input image data.

`amp::BitmapView *getBitmapView(const std::string &name);`

Returns a pointer to a named bitmap view if it exists, otherwise returns `nullptr`.

---

## Inference Crops

`std::vector<amp::PixelRect> inferenceCrops;`

Inference crops are regions of interest scheduled for model execution.
These crops are typically populated by the InferenceController.
During the inference loop, one crop is consumed per iteration.
The loop continues until the crop list becomes empty.

---

## Perception Object

`Perception *perception = nullptr;`

The Perception object travels downstream through the OpChain.
Ops may modify this object to attach persistent results.
Typical modifications include adding inference outputs, metadata, detections,
or any information that must survive beyond the lifetime of the OpChainContext.
