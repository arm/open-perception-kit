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

# MobileSAM INT8 box prompts

This dedicated CPU OpChain consumes a full image and one continuing normalized
box. It skips inference when no box is attached, forwarding video normally.
The box conditions segmentation; it does not crop the image or track an object.

## Artifact and numerical contract

The [Arm repository](https://huggingface.co/Arm/mobile-sam-int8-xnnpack-executorch/tree/cced0b5344c7f01c731df32955290adf1fe7eae1)
is pinned at `cced0b5344c7f01c731df32955290adf1fe7eae1`.
`model.json` pins the optimized `.pte` SHA-256
`5a54ac69f73e542f15cfd4f6e1c117a2c546ae6134c19507f2ce07127a42df25`
(22,800,144 bytes). The existing `scripts/download-models.py` owns artifact
staging; `config/model-assets.json` declares the author sample and expected
summary (stored locally as `segmentation.reference` to distinguish it from OPK
descriptors). Do not download model files from the running application.

The pinned [config.yaml](https://huggingface.co/Arm/mobile-sam-int8-xnnpack-executorch/blob/cced0b5344c7f01c731df32955290adf1fe7eae1/config.yaml)
and [example.py](https://huggingface.co/Arm/mobile-sam-int8-xnnpack-executorch/blob/cced0b5344c7f01c731df32955290adf1fe7eae1/example.py)
define this contract:

- Image: Float32 RGB `[1,3,1024,1024]`. Convert pixels to Float32 and divide by
  255 before stretching with bilinear half-pixel interpolation, without
  antialiasing. ImageNet normalization is inside the graph.
- Box: Float32 `[1,1,2,2]`, top-left and bottom-right corners in the model's
  continuous 1024-pixel extents. Multiply normalized coordinates by 1024.
- Outputs: Float32 logits `[1,3,256,256]` and IoU scores `[1,3]`. Select the first
  maximum-quality candidate, bilinear-upsample to 1024, threshold strictly above
  zero, then restore the binary mask to native dimensions with nearest sampling.

The author sample is 960 by 1280 pixels. Its native `(x,y,width,height)` box is
`(512,895,188,180)`, or normalized corners
`(512/960,895/1280,700/960,1075/1280)`. The pinned summary reports candidate 1,
IoU scores rounded to four decimals `(0.9725,0.9773,0.9667)` and 18,859 foreground
pixels. Rounded scores alone do not establish tensor or mask parity.

## Pipeline and destination

`config/pipelines/mobile-sam-int8-executorch.json` selects a USB camera and
includes a repeating static-image alternative. The hosting application locates
`opkprompt` as `prompts` and submits commands through public GStreamer APIs.
Launching the preset alone starts with no prompt and therefore no inference.

The selected GStreamer consumer is `destination-element=mobileSam`; the
request's `inference_instance=mobileSam` selects the internal inference Op's
explicit `instanceId`. `source_name=pipelineVideoFrame` names its input media.
These are distinct identities even though the preset uses the same spelling
for the element and the Op. Keep `opkprompt` after geometry changes in the
selected inference branch.

## Provenance and qualification

The model card declares Apache-2.0 for the original and optimized model and
identifies Arm's quantization/export of Chaoning Zhang et al.'s MobileSAM.
Pinned model-card and original-model license notices are declared in
`config/model-notices.json`. The sample image is declared CC0 by the model card,
derived from JuxtaposedJacob's Wikimedia Commons *Richmond Skyline*.
Publication and licensing declarations do not establish training-data clearance
or product approval; no separate approval record has been identified here.

The author environment pins ExecuTorch 1.1.0, Torch 2.10.0, Python 3.13,
Pillow 12.3.0 and NumPy 2.3.5. OPK packages native ExecuTorch 1.3.1-3.
Compatibility requires a real load and numerical comparison; the version
difference alone proves neither compatibility nor incompatibility.
The [implementation plan](../../../docs/arch/promptable-vision-implementation-plan.md)
defines the remaining streaming, actual CairnSDK and physical Raspberry Pi 5
acceptance gates. Check recorded implementation evidence before making
deployment or interactive-performance claims.

### Host evidence, 17 November 2026

The x86-64 Debian 13 development-container build passed with native ExecuTorch
1.3.1-3 enabled and GPU preprocessing, MLWL and Python Ops disabled. The complete
Meson run reported 97 passing targets and two expected failures, with no failed
targets. The generated SDK drift check and prompt round trips in C++, Python,
Rust and TypeScript passed. These checks do not establish device qualification.

Real model execution initially failed with `OperatorMissing` (`0x14`): the linker
discarded the SDK's primitive-registration initializers. Retaining
`libexecutorch.a` through whole-archive linking restores
`executorch_prim::et_view.default`; a native registration test guards this path.
The model then produced masks through public GStreamer commands and
SDK-decoded results, preserving unsigned request IDs above `2^63`, session,
revision and media-parent association.
The standalone native example also completed its repeating-image sequence:
submit, replace, cancel, a new request and session end. SDK decoding confirmed
both revisions, the new request and unprompted intervals in the forwarded media.

The full image-preprocessing comparison used the author's pinned Pillow 12.3.0
and CPU Torch 2.10.0. Across all 3,145,728 Float32 values, the largest absolute
error was `1.1920928955078125e-7`, below the test's declared `2e-7` bound.
This is a preprocessing tolerance, not model-level acceptance.

For the same real native-backend outputs, OPK's mask decoder matched the pinned
Torch decoder at every one of the 1,228,800 native mask bytes. Both selected
candidate 1, produced 18,844 foreground pixels and reported best score
`0.9777200222015381` when using OPK's image tensor. This isolates image packing
and mask decoding from the cross-runtime comparison below.

**Author-result qualification remains open.** With the pinned sample and box,
the published summary reports 18,859 foreground pixels and best score 0.9773.
An independent runner using the pinned Python 3.13, ExecuTorch 1.1.0, Torch
2.10.0, Pillow 12.3.0 and NumPy 2.3.5 versions on this x86 host produced 18,826
pixels and 0.9768146873. Feeding exactly that runner's image tensor to OPK's
native ExecuTorch 1.3.1-3 produced 18,848 pixels and 0.9785026908; the restored
masks disagreed at 26 pixels. Thus a runtime/environment difference exists
independently of OPK image preprocessing, and this host did not reproduce the
published author summary. No acceptance tolerance was loosened to count these
comparisons as a pass. Matching target/reference fixtures are still required.

Physical camera and Raspberry Pi 5 checks were not performed because no Pi was
available. The actual Cairn service still needs the
[command and acknowledgement bridge](../../../docs/public/how-to/box-prompts.md#cairn-integration-status).
Public documentation source preparation passed; HTML rendering was blocked by
npm-mirror connectivity. Schema additions require a future OPK minor release;
the product-version bump remains deferred to release preparation.
