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

# opktracker

`opktracker` adds tracked identities, optional predictions and traces to
FrameResults. It reads a synchronized property snapshot at each frame.

## Properties

In the table, **Live** applies without restarting. **NULL/READY** and **NULL**
requests wait for the caller to reach the indicated state. Read-only entries cannot be set.

| Property | Purpose | Default | Accepted values / Example | Changeable in |
| --- | --- | --- | --- | --- |
| `content-type` | Result layer content type containing detections to track. | `genericObject` | String matching an input layer; e.g. `humanFace`. | NULL/READY |
| `use-embeddings` | Enable ReID embedding-based association. | `true` | `true`, `false` | Live |
| `embedding-content-type` | Result layer content type containing object embeddings. | `objectEmbedding` | String matching an input layer; e.g. `objectEmbedding`. | NULL/READY |
| `embedding-weight` | Embedding contribution to hybrid matching cost. | `0.5` | Float, 0–1; e.g. `0.7`. `0` uses IoU cost only; `1` uses embedding cost when available. | Live |
| `min-cosine-similarity` | Minimum similarity for an embedding contribution. | `0.0` | Float, −1–1; e.g. `0.4`. | Live |
| `reid-reassociate-threshold` | Minimum similarity for restoring a dormant track; values ≤ 0 disable dormant reassociation. | `0.65` | Float, −1–1; e.g. `0.75`. | Live |
| `dormant-track-history-seconds` | Retention of expired track embeddings for reassociation. | `8.0` | Float seconds, 0–G_MAXFLOAT; e.g. `10.0`. | Live |
| `iou-threshold` | Minimum intersection-over-union for matching detections to tracks. | `0.3` | Float, 0–1; e.g. `0.4`. | Live |
| `max-missed-frames` | Number of unmatched frames before expiring a track. | `5` | Integer, 0–2147483647; e.g. `10`. | Live |
| `min-hits-to-confirm` | Hits required before showing a track. | `5` | Integer, 1–2147483647; e.g. `3`. | Live |
| `append-track-id-to-text` | Append `[ID:n]` to detection text. | `true` | `true`, `false` | Live |
| `trace-history-seconds` | Time window retained for track traces. | `5.0` | Float seconds, 0–G_MAXFLOAT; e.g. `2.0`. | Live |
| `kalman-dt-fallback` | Time step when timestamps are unavailable or fallback is forced. | `1/30` (about `0.0333333`) | Float seconds, 0.0001–G_MAXFLOAT; e.g. `0.04` for 25 fps. | Live |
| `kalman-dt-force-fallback` | Use the fallback time step instead of buffer timestamps. | `false` | `true`, `false` | Live |
| `kalman-initial-covariance-pos` | Initial position covariance for new tracks. | `100.0` | Float, 0–G_MAXFLOAT; e.g. `80.0`. | Live |
| `kalman-initial-covariance-vel` | Initial velocity covariance for new tracks. | `25.0` | Float, 0–G_MAXFLOAT; e.g. `30.0`. | Live |
| `kalman-process-noise-pos` | Position process noise. | `0.1` | Float, 0–G_MAXFLOAT; e.g. `0.2`. | Live |
| `kalman-process-noise-vel` | Velocity process noise. | `0.05` | Float, 0–G_MAXFLOAT; e.g. `0.1`. | Live |
| `kalman-measurement-noise-pos` | Position measurement noise. | `20.0` | Float, 0–G_MAXFLOAT; e.g. `15.0`. | Live |
| `use-kalman` | Enable Kalman prediction and measurement smoothing. | `true` | `true`, `false` | Live |
| `emit-predicted-detections` | Append predicted-only tracks while detections are missing. | `true` | `true`, `false` | Live |
| `emit-trace` | Append `trackTrace` output for active tracks. | `true` | `true`, `false` | Live |
| `association-mode` | Select the matching strategy. | `hybrid` | `hybrid` (IoU and embeddings), `iou` (IoU only), `embedding` (embeddings only). | Live |
| `infer-id` | Producer identifier attached to tracker result layers. | `""` → element name | String; e.g. `tracker`. Empty uses the element name. | NULL/READY |
| `property-status` | Latest requests, effective values and per-property outcomes. | None until requested | `GstStructure`; see [request API](../../public/how-to/configure-elements.md#requests-and-outcomes). | Read-only |

`G_MAXFLOAT` is the largest finite 32-bit float (approximately 3.4028235 × 10³⁸).
Float defaults above use their intended decimal values; introspection may display
the nearest representable float.

`content-type`, `embedding-content-type`, and `infer-id` require NULL or READY.
Their identity/source meaning must stay stable for a tracking history. Returning
to READY clears that history before the next start. Other writable properties
can change while playing: association thresholds and weights, confirmation/miss
limits, history durations, Kalman tuning and output flags. `association-mode`
accepts `iou`, `embedding`, or `hybrid`; an invalid string reports a failed
request and leaves the previous value unchanged.

Initial Kalman covariance affects new tracks only. Changing thresholds or noise
settings does not retroactively recompute existing tracks. Optional features
still depend on suitable input: embedding association needs embedding results,
and predictions/traces need tracking history. Inspect each property's range,
default and description with `gst-inspect-1.0 opktracker`.

See [Configure GStreamer elements](../../public/how-to/configure-elements.md)
for discovery, readback, validation and the explicit restart procedure.
