---
sidebar_position: 2
sidebar_label: opkosd
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


# opkosd

`opkosd` is a `GstBaseTransform` that renders `FrameResults` payloads onto OPK video
frames. It is a presentation/debugging stage and does not modify `FrameResults`
itself.

## Element Contract

- Base class: `GstBaseTransform`
- Processing mode: in-place `transform_ip`
- Pad caps: `video/x-raw, format={BGRA,RGB,I420,NV12,YUY2}`
- Metadata dependency: `FrameResultsMeta`

Explicit `video/x-raw(memory:DMABuf)` caps are also accepted for these formats.
If disabled or no `FrameResultsMeta` is attached, drawing is skipped.
`copy-input=true` still copies and negotiates system-memory output in those cases.

## Execution Model

For each frame, `opkosd` reads the immutable `FrameResults` envelope, draws supported
overlays directly onto the negotiated input frame, and returns the modified frame
downstream, retaining the metadata. Direct DMA-BUF drawing requires writable,
unshared, linear CPU-mappable memory. Otherwise the pipeline fails and suggests
`copy-input=true`; non-mappable layouts need upstream conversion.

## Rendering Model

Rendering is driven by generated payload types and explicit presentation settings.
Supported paths include:

- `BoxDetections` and `ObjectTracks`: rectangular boxes and labels, or circles
  for producers explicitly listed in `circle-producers`. `box-producers=null`
  displays other typed boxes; an explicit JSON list selects rectangle sources,
  and `[]` hides rectangles. Both properties can change while playing.
- `Classifications`: top-k text with an explicit categorical/recognized-text kind
- `PersonPresences`: centered person / non-person status from the explicit decision
- `GazeMeasurements`: finite degree-angle pairs projected from the center of an explicitly referenced, unique box or track observation; missing/ambiguous subjects and unavailable angles are omitted
- `CameraContacts`: contact status dot from the explicit outcome and unique box/track subject
- `SegmentationMasks`: explicit `RegionOpacity` masks use the existing tint;
  `BackgroundOpacity` masks use the existing background-replacement threshold
  and optional `bg-image`. Encoding, exact byte count and `ImageSpace` are validated.
  Mask meaning and placement are not inferred from content/compositing strings or parents.
- `TrackTraces`: tracker history lines
- `PerformanceMeasurements`: numeric measurements from `opkperformance`, formatted for display

Contact and gaze use explicit `subject_id` references to box/track observations.
Derivation parents and persistent track IDs are not used as fallback subjects.

## Memory And Performance

- Frames are modified in place.
- Full-frame opacity masks use their existing grid directly. Cropped opacity
  masks use a temporary surface-sized byte plane with zero coverage outside the ROI.
- Rendering is CPU-bound and assumes linear image memory.

Treat `opkosd` as a debugging overlay rather than the long-term application UI
contract.

## Properties

In the table, **Live** permits
writes in NULL, READY, PAUSED and PLAYING; **NULL/READY** and **NULL** restrict
writes to those states. Read-only entries cannot be set.

| Property | Purpose | Default | Accepted values / Example | Changeable in |
| --- | --- | --- | --- | --- |
| `enabled` | Enable drawing of result overlays. | `false` | `true`, `false` | Live |
| `performance-overlay-enabled` | Allow performance metadata to be drawn when overlays are enabled. | `true` | `true`, `false` | Live |
| `bg-image` | Replacement background for segmentation masks requesting background compositing. | `""` (none) | Image path; e.g. `/tmp/background.png`. Empty or `null` clears it. | Live |
| `copy-input` | Draw on a CPU copy instead of the original DMA-BUF. | `false` | `true`, `false` | NULL/READY |

`enabled`, `performance-overlay-enabled`, and `bg-image` can change while
playing. Background loading is synchronous and commits only after decoding
succeeds; failure posts a warning and preserves the current image/path. Empty
or null clears the background. Startup reloads the configured path and fails if
it is no longer readable. `copy-input` requires NULL or READY because it changes
output memory/caps negotiation.

See [Configure GStreamer elements](../../public/how-to/configure-elements.md)
for discovery, readback, validation and the explicit restart procedure.
