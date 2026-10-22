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

# UltraFace RFB-320 FP32 → VGF

Convert the pinned FP32 ONNX export for OPK's `UltrafaceParser`. The recipe
adds class softmax and corrects 220 anchor centres from feature-map to
image/stride coordinates. The VGF outputs probabilities and corrected SSD
offsets; trained weights and decoded boxes are preserved.

## Prepare

On Linux x86-64, create a Python 3.12 environment from the OPK root:

```sh
python3.12 -m venv .venv-ultraface
.venv-ultraface/bin/pip install numpy==2.5.2 onnx==1.23.2 onnxruntime==1.29.0 pillow==12.3.0 huggingface_hub==2.0.0 jsonschema==4.26.0
.venv-ultraface/bin/pip install --no-deps 'https://github.com/llvm/torch-mlir-release/releases/download/dev-wheels/torch_mlir-20260401.769-cp312-cp312-manylinux_2_27_x86_64.manylinux_2_28_x86_64.whl#sha256=0203f7c7b9f033d21240a46294d8a56b2a83effe68717d6f551cf67717d55a86'
```

Extract the public [model converter 0.11.0 Linux x86-64 archive](https://github.com/arm/ai-ml-sdk-model-converter/releases/tag/release/v0.11.0).
SHA-256: `6cba31b74232707cd95aab6d9194efb4761c9b81cc532e67ee19e8cfd9f429ad`.

```sh
recipe=tools/model-preparation/ultraface-rfb-320
.venv-ultraface/bin/python scripts/download-models.py --models-dir "$recipe/source"
.venv-ultraface/bin/python "$recipe/prepare.py" "$recipe/source/source.onnx" \
  artifacts/ultraface-rfb-320-mlwl --model-converter /path/to/model-converter
```

Use a new output directory. The script checks the source digest and tool versions,
then writes VGF, adapted ONNX, TOSA, descriptors, provenance and `SHA256SUMS`.
Failed conversions leave no final bundle. Keep generated binaries out of Git.

## Validate

In an [MLWL-enabled OPK runtime](../../../development/ops-mlwl/README.md), use
Python with NumPy, ONNX, ONNX Runtime, Pillow and FlatBuffers, plus `gst-launch-1.0`:

```sh
timeout --signal=TERM --kill-after=5s 10m python3 tools/model-preparation/ultraface-rfb-320/validate.py \
  development/build-active/meson-out artifacts/ultraface-rfb-320-mlwl \
  tools/model-preparation/ultraface-rfb-320/source/source.onnx /path/to/face-photo.jpg
```

Choose a photo with detectable faces. The validator compares photo → black →
photo in one MLWL session against the original ONNX, using the OpChain's
thresholds. It checks buffer reuse, boxes and confidence; require EOS and exit 0.
Tolerances: 0.01 pixels and 1e-5 confidence absolute error, both with 1e-4 relative
tolerance. Results go to `validation.json`; record the Vulkan device alongside it.
This checks output agreement, not dataset accuracy or performance.

Adjust the outer timeout for the target. For remote or simulated targets, enforce
it on the external host. MLWL cannot cancel device work: on timeout, fail the run
and recover only your test instance. Closing SSH does not stop device work.

## Use the converted model

Verify the bundle's `SHA256SUMS`, then copy `ultraface-rfb-320.vgf` beside
`config/models/ultraface-rfb-320-mlwl/model.json` in the runtime checkout.
Keep the MIT licence, provenance, validation report and checksums with the bundle.

Source: [Arm/ultraface-rfb-320-onnx-raspberry](https://huggingface.co/Arm/ultraface-rfb-320-onnx-raspberry/tree/dd894b3db6ac5ac2bcff20e5cab96b40a4402855).
Model licence: MIT (Linzaer).
