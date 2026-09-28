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

# NITEC ResNet-18 INT8 for ExecuTorch

ExecuTorch/XNNPACK variant of the NITEC camera-contact classifier.

- Source: [Arm/nitec-resnet-18-int8-xnnpack-executorch](https://huggingface.co/Arm/nitec-resnet-18-int8-xnnpack-executorch)
- Backend: ExecuTorch/XNNPACK on CPU
- Input: RGB NCHW face crop, `[1, 3, 224, 224]`, `Float32`
- Preprocessing: ImageNet mean and standard-deviation normalization
- Output: logits `[1, 2]`; class `0` is no contact and class `1` is contact
- Postprocessor: `CameraContactParser`
- Dependency: `humanFace` detections from UltraFace

The model artifact is downloaded from the pinned `hfDownload` entry and is
not stored in the repository. Run the complete UltraFace cascade with:

```bash
./tools/opk-menu nitec-resnet-18-executorch
```
