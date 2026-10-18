---
title: Use Your Own Media
sidebar_position: 1
sidebar_label: Use Your Own Media
description: Replace sample media with your own images, videos, or streams while keeping a known-good pipeline intact.
---
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


# Use Your Own Media

Use this page after a quick start works.

> We are working on providing access to a set of demo videos. Until they are
> available, please bring your own input videos.

Start from a known-working pipeline, change only the source section, and run
the pipeline again. Leave the inference, overlay, performance, and sink stages
unchanged until the new input is working.

Many checked-in video pipelines still convert to `BGRA` before inference and
overlay stages because it is the conservative known-good path. Keep that
conversion unless you are intentionally tuning pixel format behavior.

Checked-in pipeline paths use `${OPK_PROJECT_ROOT:-/work}`. `opk-menu`
substitutes the environment variable before launching GStreamer and uses
`/work` when the variable is unset or empty. When running directly from a host
checkout, set it to the repository root:

```bash
export OPK_PROJECT_ROOT="$(pwd -P)"
```

## Use an image file

Put your image under `data/images/`, then edit a pipeline under
`config/pipelines/` to use it as the source.

For a JPEG image, use this pattern:

```text
filesrc location="${OPK_PROJECT_ROOT:-/work}/data/images/my-image.jpg" !
jpegdec !
imagefreeze !
videoconvert ! video/x-raw,format=BGRA !
```

Run the same pipeline again after saving the file.

## Use a video file

Put your video under `data/videos/`, then edit a pipeline under
`config/pipelines/` to use it as the source.

For a local video file, use this pattern:

```text
filesrc location="${OPK_PROJECT_ROOT:-/work}/data/videos/my-video.mp4" !
decodebin name=dec
dec. ! queue ! videoconvert ! videoscale ! video/x-raw,format=BGRA !
```

Run the same pipeline again after saving the file.

## Use a media stream

Use the GStreamer source element that matches your stream, then keep the
downstream conversion into `BGRA`.

For example, a network or platform stream usually replaces only the source and
decode part of the pipeline. Keep the rest of the known-working pipeline in
place:

```text
<stream-source-and-decode> !
videoconvert ! videoscale ! video/x-raw,format=BGRA !
```

Checked-in pipeline presets under `config/pipelines/` include alternative
source examples. Use those as templates before changing inference or sink
behavior.

## Use a camera

For live camera input, use [Use A Camera](camera-input.md). That page covers
USB cameras, Raspberry Pi camera input, and the checks to run before changing a
pipeline.

For ready-to-run live camera presets, use:

- `full-onnx-raspicam` for a Raspberry Pi camera.
- `full-onnx-usb-cam` for a USB camera at `/dev/video0`.

## Publish the current image with results

For a simple paired-image output, set the existing publisher fragment to:

```text
opkcomm method=websocket endpoint=/ws include-frame=true !
```

Keep BGRA video before inference, place the publisher after the desired result
producers and before overlays, and restart the pipeline after changing the
property. It exports the existing video dimensions without resizing. Frame
export requires CPU system memory and is disabled by default. The option also
works with the existing file/stdout/FIFO and TCP publishing methods.

Each published JSON record includes the existing base64 FlatBuffers results
packet plus an optional `frame` object: `encoding` is `raw+base64`, `pixel_format`
is `BGRA`, `width` and `height` describe the image, `stride_bytes` is `width * 4`,
and `data_b64` contains packed pixels without row padding. Decode both from the
same message to preserve pairing. No FrameResults SDK update is needed to decode
the unchanged results packet.

This convenience path uses the existing five-job writer queue, which rejects new
jobs when full. WebSocket transport buffering is unchanged and has no added byte
limit; memory use can grow with large images or a slow consumer. A receiver must
explicitly handle the extra image field and allow its message size; existing Cairn
consumers need a separate integration update. A 640×480 BGRA image contributes about
1.56 MiB of base64 before results, exceeding Cairn's currently inspected 1 MiB
receiver default. Measure the cost at the intended resolution and frame rate.

[Back to How-To Guides](/how-to)
