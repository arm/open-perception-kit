---
sidebar_position: 14
sidebar_label: OpChain Example
---

# Example OpChain In pekinfer

This example shows a model cascade where a first model finds faces and a second
model runs once for each face crop. The pattern is: collect regions of interest,
create crops, then loop over a repeated subchain while crops remain.

## Chain Layout

The chain starts with a normal detection block and then repeats a second block
using `loopId`:

1. `InferenceController`
2. `GenericImagePreprocess`
3. `Inference`
4. `GenericPostprocess`

Each Op reads transient state from `OpChainContext` and writes persistent results
into typed `FrameResults` payloads when needed.

## Loop Semantics

`InferenceController` selects source detections by `contentType`, converts their
rectangles into crop regions, stores those crops and parent UUIDs in
`OpChainContext`, and activates the loop. One crop is consumed per iteration.
Looping stops when the crop list is empty.

This supports "for each object" inference, such as gaze estimation for each
detected face.

## Data Flow

1. A video frame is exposed as `bitmapViews["pipelineVideoFrame"]`.
2. `InferenceController` creates crop rectangles from existing `Perception`
   content.
3. `GenericImagePreprocess` builds the input tensor for the current crop.
4. `Inference` runs the model using the selected backend.
5. `GenericPostprocess` parses output tensors and writes results into
   `Perception`.

## JSON Descriptor Example

```json
{
  "version": 1,
  "name": "GazeDetectionWithUltraface",
  "description": "Detect faces, then estimate gaze for each face.",
  "displayName": "UltraFace + L2CS MobileGaze",
  "task": "Gaze estimation",
  "runtime": "ONNX",
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
        "modelDescriptor": "../../models/ultraface/model.json"
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
        "modelDescriptor": "../../models/gaze-detection/model.json"
      }
    },
    {
      "id": "pek-std-ops/GenericPostprocess",
      "loopId": 1,
      "attributes": {
        "parser": "GazeDetectionParser"
      }
    }
  ]
}
```

The same pattern applies to other runtimes by swapping the inference Op and model
descriptor. Each `modelDescriptor` path is relative to this OpChain descriptor; an
absolute filesystem path is also valid.
