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

# MobileGaze MobileNetV2 INT8 for ExecuTorch

ExecuTorch/XNNPACK variant of the MobileGaze face-level gaze estimator.

- Source: [Arm/mobilegaze-mobilenet-v2-int8-xnnpack-executorch](https://huggingface.co/Arm/mobilegaze-mobilenet-v2-int8-xnnpack-executorch)
- Backend: ExecuTorch/XNNPACK on CPU
- Input: RGB NCHW face crop, `[1, 3, 448, 448]`, `Float32`
- Preprocessing: ImageNet mean and standard-deviation normalization
- Output: two `[1, 90]` angle-logit tensors
- Postprocessor: `GazeDetectionParser` with `angleBinWidthDeg` set to `4`
- Dependency: `humanFace` detections from UltraFace

The model artifact is downloaded from the pinned `hfDownload` entry and is
not stored in the repository. Run the complete UltraFace cascade with:

```bash
./tools/opk-menu mobilegaze-mobilenet-v2-executorch
```
