---
sidebar_label: opkcomm
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

# opkcomm

`opkcomm` is a passthrough publisher. It reads `FrameResultsMeta` from each buffer,
queues shared ownership of the results, and serializes them on a writer thread.
File/stdout/FIFO, WebSocket and TCP use the existing JSON record containing
`frame_counter`, `frame_results_encoding` and `frame_results_packet_b64`.
The last field is base64 of the generated FlatBuffers packet; its encoding marker
is `perception-frame-results+base64`.

## Optional paired image

Set `include-frame=true` before starting playback to include the current video
image in the same JSON record as the results, through any existing method. The
property defaults to false. Stop the element before changing it; changes while
started are rejected with a warning. Existing method-change behavior is unchanged.

Place the element after the desired inference/tracking/performance producers and
before overlays or other pixel edits. It exports the whole video buffer at its
current dimensions, not a model's resized or letterboxed input tensor. The
feature neither resizes nor compresses images. Initial support is CPU system
memory with BGRA caps. Unsupported formats, missing results, invalid layout and
mapping failures produce GStreamer errors.

```text
... ! video/x-raw,format=BGRA ! opkinfer ... !
opkcomm method=websocket ws-port=8002 endpoint=/ws include-frame=true ! ...
```

The existing record gains one optional member:

```json
{
  "frame_counter": 42,
  "frame_results_encoding": "perception-frame-results+base64",
  "frame_results_packet_b64": "<base64 results packet>",
  "frame": {
    "encoding": "raw+base64",
    "pixel_format": "BGRA",
    "width": 640,
    "height": 480,
    "stride_bytes": 2560,
    "data_b64": "<base64 packed pixels>"
  }
}
```

The placeholders illustrate structure. Decode `data_b64` to exactly
`height * stride_bytes` bytes; `stride_bytes` is `width * 4`. Source row padding
is excluded. The results packet and FrameResults schemas are unchanged. The
same message pairs the image and results without timestamp matching or another
image timestamp. When disabled, the `frame` member is absent and pixels are not
mapped or copied.

## Ownership, buffering and stop

Capture maps the current buffer read-only using the existing video-frame
wrapper, copies active rows into owned job storage, then releases the map and
buffer reference before returning downstream. The worker receives no borrowed
pixel pointer or retained camera buffer. Results use the metadata wrapper's
shared ownership and copy-on-write mutation contract. Base64 encoding and JSON
serialization run on the existing worker, outside the streaming callback.

The existing writer queue holds at most five pending jobs and rejects new jobs
when full. Stop clears pending jobs and closes the existing writer. This feature
does not change the WebSocket writer's buffering, send admission or shutdown.
There is no added byte-budget property, fixed pixel limit or complete-record limit.

The job-count limit is not a byte or process-memory limit. Large images increase
capture, queue and serialization memory; WebSocket transport buffers can grow
when a client falls behind. Use an appropriate resolution and consumer throughput.
File/FIFO/TCP retain their existing partial-write and backpressure behavior;
frame export adds no transport delivery guarantees.
There is no replay for a disconnected subscriber. Image-size arithmetic and
layout are validated before copying.

## Integration boundary

This is a convenience path for initial feedback and debugging, not a guaranteed
high-throughput image transport. Larger raw images incur a copy and base64
expansion. A receiver must accept the complete JSON record and expose both fields.

The inspected Cairn ingress defaults to a 1 MiB WebSocket message limit and its
router extracts only the results packet. A 640×480 BGRA image alone contributes
1,638,400 base64 bytes, exceeding that limit before results/JSON. Cairn needs a
separate integration change for limits, opt-in and a combined application event;
this OPK feature does not provide end-to-end Cairn frame access. Redis remains
Cairn's responsibility. No OPK schema/SDK generation is needed for this JSON
extension.
