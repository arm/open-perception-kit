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

# Bounded bilinear image preprocessing

`opk-std-ops/GenericImagePreprocess` accepts `sampling: "bilinear"`. Omission
keeps the existing nearest-neighbour behavior, including MobileSAM. Select CPU
preprocessing: the Vulkan implementation rejects this unsupported attribute.
The former Python-plugin LinearLetterbox Op has been removed; its callers use the
generic Op and shared native helper instead.

The generic Op continues to own model dimensions, input binding, source/crop
selection, destination storage and geometry publication. The shared
[BilinearImageTensor](../../development/common/opk/BilinearImageTensor.cpp) helper
uses `ModelInput` for layout, normalization and padding, and the existing
[ImageResize](../../development/common/opk/ImageResize.cpp) uint8 bilinear primitive.
It is also used by the YOLO-E visual-reference adapter.

Supported recipe:

- Interleaved uint8 RGB or BGRA source; RGB channel selection discards alpha.
- Fixed batch-one Float32 RGB CHW, BGR CHW or RGB HWC destination, including
  rectangular canvases, with `keepAspectRatio: true`.
- Centered aspect fit, allowing upscaling; half-pixel bilinear interpolation with
  replicated edges and the previously qualified fixed-point uint8 arithmetic.
- Resized dimensions round ties to even. Odd padding leaves the extra pixel on
  the right/bottom. Collapsed dimensions are rejected.
- Pixel values divide by the uint8 maximum before model mean/std normalization.
  Padding is the model's normalized RGB color, then mean/std normalized. BGR CHW
  mean/std parameters follow tensor channel order, as in the existing model contract.

The generic nearest kernels use different size rounding and reciprocal-multiply
normalization paths. Reusing those arithmetic paths would change qualified tensors;
the bilinear helper preserves division and integer-resize rounding explicitly.
Geometry is returned with the pixels and used directly for inference metadata,
so decoding does not recompute a potentially different resize rectangle.

There is no neural-network side limit of 4096. Source spans, stride and destination
byte products are checked before access/allocation, and allocation errors become
OPK errors. The caller/model still determines memory requirements. No auto-stride
padding, scale-fill policy, GPU bilinear path, quantized output conversion or
additional detector-specific recipe has been added.

YOLO-E's 640-pixel inputs, 80-pixel region maps, prompt conditioning and encoder
constraints remain in its adapters. A tensor format supported here does not prove
that every model using that format has matching preprocessing semantics.

## Qualification

The original YOLO-E pixel fixtures cover odd aspect ratios and RGB/BGRA sources;
they remain byte-identical. Text and visual real-model tests also pass with the
new generic target preprocessing path. The pinned author geometry/interpolation
recipe is in [YOLO-E LetterBox](https://github.com/THU-MIG/yoloe/blob/40cd606cabdbe2b566d6f14a6b162c89206e9a1b/ultralytics/data/augment.py).

A second concrete export is the existing
[YOLO26n-320 descriptor](../../config/models/yolo26n-320/model.json):
`Arm/yolo26n-320-int8-onnx-raspberrypi5`, revision
`0a78551c6225bb4947a6397a3d46cfaf91c00812`, artifact SHA-256
`a64a843197d4e7697c2d834d610fba88a7ebd81a62f39d1270ea7ad1aed687fc`.
Its inspected graph input is Float32 `[1,3,320,320]`, output `[1,300,6]`.
Using the pinned author bus image (810 by 1080), the generic Op produced a
240 by 320 inner image with 40 pixels of left/right padding. Its input tensor
matched the independent OpenCV 4.12.0 oracle byte-for-byte; native ONNX outputs
matched the ONNX Runtime 1.23.2 oracle within absolute tolerance `1e-4`.
This is a numerical reuse demonstration, not a dataset accuracy qualification.
The existing YOLO26 production presets retain their current sampling defaults.

The model is reused from the existing model download owner's output; no alternate
downloader is introduced. To regenerate fixtures in the isolated export environment:

```sh
python tools/preprocessing/qualify_bilinear.py \
  --model-descriptor config/models/yolo26n-320/model.json \
  --image .cache/expkits-1606/gate0/source/THU-MIG-yoloe-40cd606/ultralytics/assets/bus.jpg \
  --output .cache/expkits-1606/qualified-native
```

The script checks the descriptor's artifact SHA-256 and records input shape, image
hash and dependency versions. `query_model_tests` consumes these files when
`OPK_QUERY_TEST_ASSETS` points to that directory. Missing optional model fixtures
are reported as skipped; the qualification run described above had no skips.
OpenCV remains confined to the oracle/export environment, not the OPK runtime.
