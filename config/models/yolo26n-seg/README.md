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

# YOLO26n Instance Segmentation (640x640)

Full-frame instance segmentation model that predicts a separate mask, bounding
box, confidence score, and COCO class ID for each detected object.

- Source: [Ultralytics assets v8.4.0](https://github.com/ultralytics/assets/releases/download/v8.4.0/yolo26n-seg.onnx)
- Backend: ONNX Runtime on CPU
- Input: RGB NCHW image, `[1, 3, 640, 640]`, `Float32`
- Preprocessing: values in `[0, 1]`, with aspect-ratio-preserving letterboxing and padding value `114/255`
- Output: detection rows `[1, 300, 38]` and mask prototypes `[1, 32, 160, 160]`, both `Float32`
- Postprocessor: `Yolo26InstanceSegmentationParser`, confidence threshold `0.25`; end-to-end model output requires no additional NMS
- FrameResults payload: `InstanceSegmentationMasksT` with `contentType` set to `instanceSegmentation` and `labelFamily` set to `coco`; each `InstanceSegmentationMaskT` carries object metadata, a binary bitmap, class ID, confidence, and source-coordinate bounding box

Run the full ONNX preset inside the OPK container:

```bash
./tools/opk-menu full-onnx
```
