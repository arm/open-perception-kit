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

# YOLO26n Semantic Segmentation (1024x1024)

Full-frame semantic segmentation model trained on Cityscapes that assigns a
class ID to every pixel. Objects of the same class share the same label.

- Source: [Ultralytics assets v8.4.0](https://github.com/ultralytics/assets/releases/download/v8.4.0/yolo26n-sem.onnx)
- Backend: ONNX Runtime on CPU
- Input: RGB NCHW image, `[1, 3, 1024, 1024]`, `Float32`
- Preprocessing: values in `[0, 1]`, with aspect-ratio-preserving letterboxing and padding value `114/255`
- Output: per-pixel class IDs `[1, 1024, 1024]`, `Uint8`
- Postprocessor: `Yolo26SemanticSegmentationParser`; the exported class map needs no confidence threshold or NMS
- FrameResults payload: `SemanticSegmentationMasksT` with `contentType` set to `semanticSegmentation` and `labelFamily` set to `cityscapes`; one source-sized `Uint8` class-ID bitmap
- Classes: 19 Cityscapes training classes, IDs `0` through `18`; class `0` is road and is a valid class

Run the full ONNX preset inside the OPK container:

```bash
./tools/opk-menu full-onnx
```
