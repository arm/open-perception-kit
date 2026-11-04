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

# PP-OCRv6 tiny text detector

Full-frame text-region detector for horizontal OCR.

- Backend: ONNX Runtime on CPU
- Input: BGR NCHW image, `[1, 3, 736, 736]`, `Float32`
- Preprocessing: centered aspect fitting with normalized zero padding
- Normalization: mean and standard deviation from `model.json`, in BGR order
- Output: Float32 probability map `[1, 1, H, W]`
- Postprocessor: `PaddleOcrDbDetectionParser`
- FrameResults payload: `BoxDetectionsT` with `contentType` set to `textRegion`

Place the local `PP-OCRv6_tiny_det.onnx` artifact beside `model.json`.
Resize, padding, and detector filtering settings live in the
[combined OpChain](../../opchains/pp-ocrv6-tiny/opchain.json).
The integration uses axis-aligned crops without perspective correction.

Run the complete OCR preset inside the OPK container:

```bash
./tools/opk-menu pp-ocrv6-tiny
```
