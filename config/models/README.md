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

# Model catalog

This directory contains the supported ONNX and ExecuTorch model configurations.
The quick-start development container downloads model artifacts from pinned
Hugging Face revisions using the user's access. Binaries are neither committed
nor included in release packages or deployment/Cairn images. See
[Download models](../../docs/public/getting-started/binary-release.md#download-models)
for using the existing downloader with a binary release.

- [UltraFace RFB-320 INT8](https://huggingface.co/Arm/ultraface-rfb-320-onnx-raspberry)
- [YOLO26n INT8, 320 pixels](https://huggingface.co/Arm/yolo26n-320-int8-onnx-raspberrypi5)
- [YOLO26n INT8, 480 pixels](https://huggingface.co/Arm/yolo26n-480-int8-onnx-raspberrypi5)
- [YOLO26n INT8, 640 pixels](https://huggingface.co/Arm/yolo26n-640-int8-onnx-raspberrypi5)
- [YOLO26s INT8, 320 pixels](https://huggingface.co/Arm/yolo26s-320-int8-onnx-raspberrypi5)
- [YOLO26s INT8, 480 pixels](https://huggingface.co/Arm/yolo26s-480-int8-onnx-raspberrypi5)
- [YOLO26s INT8, 640 pixels](https://huggingface.co/Arm/yolo26s-640-int8-onnx-raspberrypi5)
- [OSNet x0.25 INT8](https://huggingface.co/Arm/osnet-x0-25-int8-mlas-onnx-raspberrypi5)
- NITEC ResNet-18 INT8: [ONNX](https://huggingface.co/Arm/nitec-resnet-18-int8-onnx), [ExecuTorch/XNNPACK](https://huggingface.co/Arm/nitec-resnet-18-int8-xnnpack-executorch)
- MobileGaze MobileNetV2 INT8: [ONNX](https://huggingface.co/Arm/mobilegaze-mobilenet-v2-int8-onnx), [ExecuTorch/XNNPACK](https://huggingface.co/Arm/mobilegaze-mobilenet-v2-int8-xnnpack-executorch)
