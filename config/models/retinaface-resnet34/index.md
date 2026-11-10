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

# RetinaFace ResNet34 ONNX

Runs the original [upstream ONNX export](https://github.com/yakhyo/retinaface-pytorch/releases/tag/v0.0.1)
on the ONNX Runtime CPU backend. The two model descriptors share one weight file:
1080×1080 and 1920×1080 (width × height). The input is FP32 NCHW BGR with
`[104,117,123]` subtracted from the pixel values.

`RetinaFaceParser` decodes the three-scale anchors and applies confidence
filtering and NMS. It publishes face boxes in frame pixels for overlays and
secondary inference. The landmark output is checked but is not published.

The pinned ONNX file records PyTorch 2.4.0 as its producer, IR version 6
and the standard ONNX opset 11. Validated with ONNX Runtime 1.31.1
on Linux x86_64 CPU.

The [model asset manifest](../../model-assets.json) pins the upstream URL and
SHA-256; `scripts/download-models.py` downloads it once. The MIT licence is
included by [model-notices.json](../../model-notices.json).

After the quick start and OPK build, switch between all four combinations
in the WebUI with:

```sh
./scripts/run.sh retinaface-gaze-onnx
```

Use the Model selector to disable the current combination and enable the next.
Keep one combination active at a time. Add Bounding boxes and Gaze in Overlays;
select the active model outputs when changing the detector size or gaze backbone.

To run a single combination:

```sh
./scripts/run.sh retinaface-1080x1080-gaze-resnet34
./scripts/run.sh retinaface-1080x1080-gaze-resnet50
./scripts/run.sh retinaface-1920x1080-gaze-resnet34
./scripts/run.sh retinaface-1920x1080-gaze-resnet50
```

Each uses the bundled gaze demo video. Change the source in the matching
`config/pipelines/` preset for your own video or camera. Face crops are resized
through OPK's existing nearest-neighbour image preprocessing.

For a headless integration check with the downloaded models and a local image
containing faces, run inside the development container:

```sh
PYTHONPATH=generated/open_perception_kit/python/src \
  python3 development/tests/face_gaze_pipeline_test.py \
  development/build-active/meson-out /path/to/faces.jpg
```

This checks all four chains, face-to-gaze links and the no-face path. It does
not measure labeled gaze accuracy or compare resize behavior with upstream demos.
