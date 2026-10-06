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

# YOLO26s INT8 (480x480)

Small full-frame object detector optimized for ONNX Runtime on Raspberry Pi 5.

- Source: [Arm/yolo26s-480-int8-onnx-raspberrypi5](https://huggingface.co/Arm/yolo26s-480-int8-onnx-raspberrypi5)
- Backend: ONNX Runtime on CPU
- Input: RGB NCHW image, `[1, 3, 480, 480]`, with aspect-ratio-preserving letterboxing
- Output: decoded corner coordinates, score, and COCO class ID
- Postprocessor: `YoloParser` using `cornerScoreClass`; model output already includes NMS
- FrameResults payload: `BoxDetectionsT` with `contentType` set to `genericObject`

The model binary is downloaded from the pinned `hfDownload` entry in
`model.json`; it is not stored in the repository.

```bash
./tools/opk-menu yolo26s-480
```
