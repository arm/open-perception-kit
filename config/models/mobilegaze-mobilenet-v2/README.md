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

# MobileGaze MobileNetV2 INT8

Face-level gaze estimator for yaw and pitch.

- Source: [Arm/mobilegaze-mobilenet-v2-int8-onnx](https://huggingface.co/Arm/mobilegaze-mobilenet-v2-int8-onnx)
- Backend: ONNX Runtime on CPU
- Input: RGB NCHW face crop, `[1, 3, 448, 448]`, `Float32`
- Preprocessing: ImageNet mean and standard-deviation normalization
- Output: two `[1, 90]` angle-logit tensors
- Postprocessor: `GazeDetectionParser` with `angleBinWidthDeg` set to `4`
- FrameResults payload: `GazeMeasurementsT` with explicit subject IDs and optional degree-angle pairs
- Input: `BoxDetectionsT` from an explicitly connected face detector, such as UltraFace.

The model-local OpChain processes existing face detections. The focused
pipeline uses `config/opchains/mobilegaze-mobilenet-v2/opchain.json` to run
UltraFace first. The model binary is downloaded from the pinned `hfDownload`
entry in `model.json`; it is not stored in the repository.

Yaw and pitch use the unmirrored image convention: near forward gaze, positive
yaw points left and positive pitch points up. The configured four-degree bins
span `-180` to `176` degrees. A failed/non-finite axis makes the pair unavailable;
measured `(0, 0)` is still a valid result. The confidence score is the minimum
of the two maximum softmax-bin probabilities, not a calibrated probability that
the gaze is correct. See the [gaze metadata contract](../../../docs/arch/perception.md#gaze-measurements).

Consumers identify gaze by its SDK payload type and concrete producer identity.
The face-crop input is selected by `sourceOp` or an external input binding.

Run the complete focused preset inside the OPK container:

```bash
./tools/opk-menu mobilegaze-mobilenet-v2
```
