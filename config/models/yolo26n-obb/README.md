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

# YOLO26n oriented boxes

- Source: [YOLO26 ONNX exports](https://huggingface.co/prithivMLmods/YOLO26-ONNX) from the [Ultralytics YOLO26n-OBB model](https://docs.ultralytics.com/tasks/obb/)
- License: AGPL-3.0 (model metadata)
- Input: RGB NCHW, `[1, 3, 640, 640]`, with letterboxing
- Output: `[1, 300, 7]` rows of center x/y, width, height, confidence, DOTA class ID, and angle in radians
- Postprocessor: `YoloParser` using `centerSizeScoreClassAngle`; the export has an end-to-end head, so NMS is disabled
- FrameResults payload: `OrientedBoxDetectionsT` in original-frame pixels

The pinned artifact is downloaded by `scripts/download-models.py`; the binary is not committed.
The bundled sample video may contain few DOTA objects. Use aerial imagery for a useful demo.
This preset omits `opktracker` because tracking currently consumes axis-aligned
`BoxDetections`; browser OSD renders the oriented payload directly.

```bash
./tools/opk-menu yolo26n-obb
```
