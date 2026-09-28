---
sidebar_position: 14
sidebar_label: OpChain Example
---
<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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


# Example OpChain In opkinfer

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
2. `InferenceController` creates crop rectangles from existing `FrameResults`
   content.
3. `GenericImagePreprocess` builds the input tensor for the current crop.
4. `Inference` runs the model using the selected backend.
5. `GenericPostprocess` parses output tensors and writes results into
   `FrameResults`.

## JSON Descriptor Example

```json
{
  "version": "1.0.0",
  "name": "GazeDetectionWithUltraface",
  "description": "Detect faces, then estimate gaze for each face.",
  "displayName": "UltraFace + L2CS MobileGaze",
  "task": "Gaze estimation",
  "runtime": "ONNX",
  "ops": [
    {
      "id": "opk-std-ops/InferenceController",
      "attributes": {}
    },
    {
      "id": "opk-std-ops/GenericImagePreprocess",
      "attributes": {
        "inputImageTensorIndex": 0,
        "inputImageSourceName": "pipelineVideoFrame"
      }
    },
    {
      "id": "opk-onnx-ops/Inference",
      "attributes": {
        "modelDescriptor": "../../models/ultraface/model.json"
      }
    },
    {
      "id": "opk-std-ops/GenericPostprocess",
      "attributes": {
        "parser": "UltrafaceParser",
        "normalizeOutputCoordinates": false,
        "confidenceThreshold": 0.3,
        "iouThreshold": 0.1
      }
    },
    {
      "id": "opk-std-ops/InferenceController",
      "loopId": 1,
      "attributes": {
        "contentType": "humanFace"
      }
    },
    {
      "id": "opk-std-ops/GenericImagePreprocess",
      "loopId": 1,
      "attributes": {
        "inputImageTensorIndex": 0,
        "inputImageSourceName": "pipelineVideoFrame"
      }
    },
    {
      "id": "opk-onnx-ops/Inference",
      "loopId": 1,
      "attributes": {
        "modelDescriptor": "../../models/mobilegaze-mobilenet-v2/model.json"
      }
    },
    {
      "id": "opk-std-ops/GenericPostprocess",
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
