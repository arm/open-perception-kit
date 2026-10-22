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

# YOLO26n-320 FP32 / MLWL

Enable the [MLWL backend](../../../development/ops-mlwl/README.md) and place
`yolo26n-320.vgf` beside `model.json`. Supply the VGF separately, then run this
folder's `opchain.json` in an OPK pipeline.
See the [conversion and validation recipe](../../../tools/model-preparation/yolo26n-320/README.md).

- Input: FP32 RGB/NCHW `[1,3,320,320]`, scaled by `1/255`, with
  aspect-preserving letterboxing and RGB padding `114`.
- Output: `[1,2100,84]`, decoded box corners and 80 COCO class probabilities.
- `YoloParser`: `cornerClassScores`, `maxDetections=300`, confidence `0.4`,
  no NMS. TopK ranks anchors by best class, then class/anchor pairs. Ties keep
  input order; multiple classes from one anchor can survive.

Prepare the **original FP32 ONNX** with `/model.23/Transpose_output_0` as its
sole output. Preserve learned layers, box decoding and nearest-floor upsampling.

Expected VGF SHA-256:
`c0c23b17e78ba2698c80c7558e470cc57376519b2e5a8ef0d50e5f881d0955b1`.

Source: [Arm's YOLO26n-320 model repository](https://huggingface.co/Arm/yolo26n-320-int8-onnx-raspberrypi5),
revision `d85636720c31b6bc65d91b2c785d24fbdb283a61`, file
`yolo26n_raspberry_onnx_original.onnx`, SHA-256
`d38ee776e79832560a35875d8962e8ff737f9ea1274d54b785fda2861d7b8ef2`.
Conversion used Torch-MLIR `20260401.769` and model-converter `0.11.0`.
Model licence: **AGPL-3.0**.
