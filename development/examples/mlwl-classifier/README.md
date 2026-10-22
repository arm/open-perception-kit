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

# VGF classifier with MLWL

Build the [MLWL backend](../../ops-mlwl/README.md) and place your `classifier.vgf`
beside `model.json`. Compile it for your Vulkan runtime; OPK does not convert models.

The supplied descriptor expects:

- Input: contiguous Float32 RGB/NCHW `[1,3,224,224]`, resized to 224×224,
  with ImageNet mean/std normalization of values in `[0,1]`.
- Output: Float32 logits `[1,1001]` in OPK's ImageNet label order, including
  background at index 0. `ImageNetClassificationParser` applies softmax.

Match tensor order, shapes, types and preprocessing to the VGF. For another
model, adapt the descriptors and choose a parser that matches its outputs.

## Run

Inside the built OPK environment, from the repository root:

```sh
gst-launch-1.0 -e \
  filesrc location=/path/to/image.jpg ! jpegdec ! videoconvert ! \
  video/x-raw,format=BGRA ! \
  opkinfer opchain-path=development/examples/mlwl-classifier/opchain.json ! \
  opkcomm method=file file-name=/tmp/mlwl-results.ndjson ! fakesink sync=false
```

`opkcomm` writes base64 FrameResults packets in NDJSON for the OPK SDK.
For a direct single-image run:

```sh
tools/opchain-exec development/examples/mlwl-classifier/opchain.json /path/to/image.jpg
```
