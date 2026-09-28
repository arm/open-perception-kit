<!-- SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates -->

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
