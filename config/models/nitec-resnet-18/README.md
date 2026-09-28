<!-- SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates -->

# NITEC ResNet-18 INT8

Binary camera-contact classifier for detected face crops.

- Source: [Arm/nitec-resnet-18-int8-onnx](https://huggingface.co/Arm/nitec-resnet-18-int8-onnx)
- Backend: ONNX Runtime on CPU
- Input: RGB NCHW face crop, `[1, 3, 224, 224]`, `Float32`
- Preprocessing: ImageNet mean and standard-deviation normalization
- Output: logits `[1, 2]`; class `0` is no contact and class `1` is contact
- Postprocessor: `CameraContactParser`
- FrameResults payload: `ClassificationsT` with `contentType` set to `cameraContact`
- Dependency: `humanFace` detections from UltraFace

The model-local OpChain processes existing face detections. The focused
pipeline uses `config/opchains/nitec-resnet-18/opchain.json` to run UltraFace
first. The model binary is downloaded from the pinned `hfDownload` entry in
`model.json`; it is not stored in the repository.

Run the complete focused preset inside the OPK container:

```bash
./tools/opk-menu nitec-resnet-18
```
