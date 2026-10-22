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

# YOLO26n-320 FP32 → VGF

Convert the original **FP32 ONNX** from Arm's model repository. The model retains
its **AGPL-3.0** licence. Keep generated binaries out of Git and include the source
model card when sharing.

## Prepare

Use the same pinned Python 3.12 environment and model-converter `0.11.0` as
the [UltraFace recipe](../ultraface-rfb-320/README.md#prepare).
From the OPK root:

```sh
recipe=tools/model-preparation/yolo26n-320
.venv-ultraface/bin/python scripts/download-models.py --models-dir "$recipe/source"
.venv-ultraface/bin/python "$recipe/prepare.py" "$recipe/source/source.onnx" \
  artifacts/yolo26n-320-mlwl --model-converter /path/to/model-converter
```

Use a new output directory. The script checks the source digest and tool versions,
then writes VGF, adapted ONNX, TOSA, descriptors, provenance and `SHA256SUMS`.
Failed conversions leave no final bundle.

The recipe preserves learned weights and box decoding, with two adaptations:

- Export decoded boxes and class probabilities as `[1,2100,84]`. OPK's
  `YoloParser` performs the original two-stage TopK; see the
  [model configuration](../../../config/models/yolo26n-320-mlwl/index.md).
- Replace both 2× nearest-floor resizes with exact row/column replication to
  avoid the pinned Torch-MLIR version's nearest-rounding behaviour.

Verify `SHA256SUMS` before using or sharing the bundle.

## Validate

On the development host, use a photo with detectable COCO objects:

```sh
.venv-ultraface/bin/python "$recipe/validate.py" prepare \
  "$recipe/source/source.onnx" /path/to/photo.jpg artifacts/yolo26n-cases
```

In an [MLWL-enabled OPK runtime](../../../development/ops-mlwl/README.md), mount
the bundle at `/model`, cases at `/cases`, and a writable result directory at
`/results`. Select the intended Vulkan ICD/layers, then run:

```sh
gst-launch-1.0 -e filesrc location=/cases/photo-black-photo.bgra ! \
  rawvideoparse format=bgra width=320 height=320 framerate=1/1 ! \
  opkinfer opchain-path=/model/opchain.json ! \
  opkcomm method=file file-name=/results/frames.ndjson ! fakesink sync=false
```

Require EOS and exit 0. Copy the packets back and compare on the host, with
the FlatBuffers Python runtime installed for the generated OPK SDK:

```sh
.venv-ultraface/bin/python "$recipe/validate.py" check \
  artifacts/yolo26n-cases/expected.json /path/to/results/frames.ndjson
```

This compares photo → black → photo in one session against the **full original
ONNX, including TopK**. It checks buffer reuse, class IDs, boxes and confidence.
Tolerances: 0.01 pixels and 1e-5 confidence absolute error, both with 1e-4 relative
tolerance. This checks output agreement, not dataset accuracy or performance.

Record VGF/runtime hashes, Vulkan device, host elapsed time and exit status.
On simulated targets, allow enough host time for compilation and all three frames.
MLWL cannot cancel device work: on timeout, fail the run and recover only your
test instance. Closing SSH does not stop device work.
