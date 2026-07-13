---
sidebar_position: 13
sidebar_label: OpChain Example
---

# Example OpChain in pekinfer

## Model Cascading: Face-Driven Crop Loop With Preprocess → Inference → Postprocess

This example shows a typical OpChain executed inside the `pekinfer` GStreamer element.
The chain runs inference over multiple regions of interest derived from Perception content.
The main pattern is: collect ROIs → create crops → loop over a subchain while crops remain.

---

## Chain Layout

The chain is composed of four Ops executed once, followed by a second four-Op block that is repeated with `loopId = 2`.
Each Op can read transient execution data from OpChainContext and write persistent results into Perception.

- `pek-std-ops/InferenceController`
- `pek-std-ops/GenericImagePreprocess`
- `pek-onnx-ops/Inference`
- `pek-std-ops/GenericPostprocess`

---

## InferenceController Loop Semantics

`InferenceController` drives the multi-crop execution flow.
It selects input regions based on `contentType`.
It queries Perception for all rectangles matching the content type (e.g. detected faces).
It converts these rectangles into logical crops for inference.
It pushes the resulting crop list into `OpChainContext::inferenceImageCrops` and stores matching parent UUIDs in `OpChainContext::inferenceImageCropUuids`.
It then repeatedly executes the Ops that share the active `loopId` while consuming crops.
One crop is removed per iteration.
Looping stops when the crop list becomes empty.

This allows a single frame to produce multiple inference executions.
This design supports “for each object” inference, such as “for each detected face” gaze estimation.

---

## Data Flow Summary

A video frame enters the pipeline and is exposed as `bitmapViews["pipelineVideoFrame"]`.
InferenceController creates crop rectangles based on Perception content.
GenericImagePreprocess builds the input tensor for the current crop.
Inference runs the model for the current crop using the selected runtime backend.
GenericPostprocess parses output tensors and writes structured metadata into Perception.

---

## JSON Descriptor Example

The OpChain is defined declaratively via JSON.
The `loopId` field ties Ops into a repeated execution group.
`InferenceController` activates the loop by setting the current `loopId` in the execution context.

```json
{
  "ops": [
    {
      "id": "pek-std-ops/InferenceController",
      "attributes": {}
    },
    {
      "id": "pek-std-ops/GenericImagePreprocess",
      "attributes": {
        "inputImageTensorIndex": 0,
        "inputImageSourceName": "pipelineVideoFrame"
      }
    },
    {
      "id": "pek-onnx-ops/Inference",
      "attributes": {
        "modelDescriptor": "/work/config/models/ultraface/model.json"
      }
    },
    {
      "id": "pek-std-ops/GenericPostprocess",
      "attributes": {
        "parser": "UltrafaceParser",
        "normalizeOutputCoordinates": false,
        "confidenceThreshold": 0.3,
        "iouThreshold": 0.1
      }
    },
    {
      "id": "pek-std-ops/InferenceController",
      "loopId": 1,
      "attributes": {
        "contentType": "humanFace"
      }
    },
    {
      "id": "pek-std-ops/GenericImagePreprocess",
      "loopId": 1,
      "attributes": {
        "inputImageTensorIndex": 0,
        "inputImageSourceName": "pipelineVideoFrame"
      }
    },
    {
      "id": "pek-onnx-ops/Inference",
      "loopId": 1,
      "attributes": {
        "modelDescriptor": "/work/config/models/gaze-detection/model.json"
      }
    },
    {
      "id": "pek-std-ops/GenericPostprocess",
      "loopId": 1,
      "attributes": {
        "parser": "GazeDetectionParser",
        "normalizeOutputCoordinates": false,
        "confidenceThreshold": 0.5,
        "iouThreshold": 0.3
      }
    }
  ]
}
```

## Notes

The example uses ONNX inference via pek-onnx-ops/Inference.
The same pattern applies to other runtimes by swapping the inference Op implementation.
Perception is the persistent container that travels downstream and accumulates results across Ops and across GStreamer pekinfer element instances.
OpChainContext is transient and only valid during execution of the current chain.
