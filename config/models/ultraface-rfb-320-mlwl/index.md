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

# UltraFace RFB-320 FP32 / MLWL

Enable the [MLWL backend](../../../development/ops-mlwl/README.md) and place
`ultraface-rfb-320.vgf` beside `model.json`. Supply the VGF separately.
See the [conversion and validation recipe](../../../tools/model-preparation/ultraface-rfb-320/README.md).

- Input: FP32 RGB/NCHW `[1,3,240,320]`, stretched without letterboxing,
  normalized as `(RGB8 - 127) / 128`.
- Outputs: class probabilities `[1,4420,2]` and corrected SSD offsets
  `[1,4420,4]`. Use the OPK-adapted export; raw model heads will not work.
- `UltrafaceParser`: confidence threshold `0.7`, IoU threshold `0.3`.

Run from the OPK root:

```sh
./tools/opchain-exec config/models/ultraface-rfb-320-mlwl/opchain.json /path/to/face-photo.jpg
```

Expected VGF SHA-256:
`665e3f0ef67ea93a0b21c8acf3d33604ef4c3f82bd61860887beb35f5adba471`.

Source: [Arm/ultraface-rfb-320-onnx-raspberry](https://huggingface.co/Arm/ultraface-rfb-320-onnx-raspberry/tree/dd894b3db6ac5ac2bcff20e5cab96b40a4402855).
Model licence: MIT. Validated with MLWL 0.11.0 and software Vulkan emulation;
check compatibility when using another driver.
