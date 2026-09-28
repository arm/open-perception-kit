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

# OSNet x0.25 INT8

Object re-identification model for embedding detected object crops.

- Source: [Arm/osnet-x0-25-int8-mlas-onnx-raspberrypi5](https://huggingface.co/Arm/osnet-x0-25-int8-mlas-onnx-raspberrypi5)
- Backend: ONNX Runtime on CPU
- Input: RGB NCHW object crop, `[1, 3, 256, 128]`
- Preprocessing: ImageNet mean and standard-deviation normalization
- Output: dynamically discovered embedding tensor, typically `[1, 512]`
- Postprocessor: `ObjectEmbeddingParser`
- FrameResults payload: `ObjectEmbeddingsT` with `contentType` set to `objectEmbedding`
- Dependency: `genericObject` detections from YOLO26n-320

The model-local OpChain processes existing object detections. The focused
pipeline uses `config/opchains/osnet-x0-25/opchain.json` to run YOLO26n-320
first. The model binary is downloaded from the pinned `hfDownload` entry in
`model.json`; it is not stored in the repository.

Run the complete focused preset inside the OPK container:

```bash
./tools/opk-menu osnet-x0-25
```
