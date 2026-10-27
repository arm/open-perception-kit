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

# PP-OCRv6 tiny text recognizer

Text recognizer for horizontal, axis-aligned detector crops.

- Backend: ONNX Runtime on CPU
- Input: BGR NCHW text crop, `[1, 3, 48, 320]`, `Float32`
- Preprocessing: height fitting, width compression when needed, and normalized zero right padding
- Normalization: mean and standard deviation from `model.json`, in BGR order
- Output: Float32 character probabilities `[1, T, C]`
- Postprocessor: `PaddleOcrRecognitionParser` with greedy CTC decoding
- FrameResults payload: `ClassificationsT` with `contentType` set to `textRecognition`
- Dependency: `textRegion` detections from the PP-OCRv6 tiny detector

Place the local `PP-OCRv6_tiny_rec.onnx` artifact beside `model.json`.
Keep `characters.txt` in the `PostProcess.character_dict` order from the supplied
`PP-OCRv6_tiny_rec_inference_opt.yml`; do not add blank or space entries.
The parser adds CTC blank at index zero and appends space. The
[combined chain](../../opchains/pp-ocrv6-tiny/index.md) configures resizing,
padding, and `dictionaryFile`, and links recognized text to each detected region.

Run the complete preset inside the OPK container:

```bash
./tools/opk-menu pp-ocrv6-tiny
```
