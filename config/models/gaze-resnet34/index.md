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

# Gaze ResNet34 ONNX

Runs the original [upstream ONNX export](https://github.com/yakhyo/gaze-estimation/releases/tag/weights)
on ONNX Runtime CPU. Input: FP32 RGB `[1,3,448,448]`, scaled to `[0,1]` and
normalized with ImageNet mean/std. Output: yaw and pitch logits, each `[1,90]`.

The pinned ONNX file records PyTorch 2.8.0 as its producer, IR version 9
and the standard ONNX opset 20. Validated with ONNX Runtime 1.31.1
on Linux x86_64 CPU.

The existing `GazeDetectionParser` uses `angleBinWidthDeg: 4`, matching
`sum(softmax(logits) * bin) * 4 - 180` degrees. The existing face-crop loop links
each gaze result to its RetinaFace detection. This model has distinct weights
from the already integrated MobileGaze MobileNetV2; it reuses that parser and
loop structure.

See [RetinaFace pipelines](../retinaface-resnet34/index.md) for both detector
resolutions. Downloads are pinned in [model-assets.json](../../model-assets.json).
The existing upstream MIT notice is `licenses/mobilegaze-LICENSE.txt`.
