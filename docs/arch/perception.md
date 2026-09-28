---
sidebar_position: 10
sidebar_label: FrameResults schema
---
<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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


# FrameResults schema

The `open_perception_kit.metadata` schema defines structured runtime results.
`open_perception_kit::FrameResults` is the concrete C++ runtime container: a generated,
typed envelope that travels downstream with a media buffer and accumulates
payloads from postprocessing, tracking, and performance elements.

The public SDK packages and C++ facade use **open-perception-kit** names;
`open_perception_kit` is the packet producer identity.
**FrameResults** identifies one frame's runtime result envelope.

## Generated SDKs

The canonical schema and generated output directories are declared in
`tools/perception/sdk.json`. Run `./scripts/perception-sdk.sh generate` to
regenerate the checked-in C++, Python, Rust, and TypeScript SDKs; use
`./scripts/perception-sdk.sh check` in CI to detect drift.

Development generation and release packaging are intentionally separate.
`$regenerate-perception-sdk` updates tracked generated sources during
implementation. `$package-open-perception-kit-release` consumes an already committed
snapshot and creates distributable artifacts without regenerating it.

`tools/perception/sdk.json` is the only hand-edited SDK release descriptor. It
defines the wire identity, public package name, canonical schema and generated directories,
flowdata-sdk location, generated project integrations, and checksum-locked
FlatBuffers and Python wheel-build artifacts. The SDK packages share the OPK
product version from `development/meson.build`. `tools/perception/sdk_config.py` is the shared loader
used by generation, packaging, tests, and development installation. Generation first verifies the raw
flowdata manifests, then applies OPK-owned copyright and formatting decoration. The final
generated SDK manifest embeds the raw generator manifests and records the
descriptor hash, decorated file hashes, and derived project integrations.
Generation also refreshes the exact Plumber SDK dependency and the checked-in
WebUI bundle that consumes the TypeScript SDK; `check` verifies both.

The Flowdata generator sources are normal tracked files under
`tools/flowdata-sdk`. Updates are manual source changes, reviewed together with
regenerated SDK outputs. Generation receipts identify the local generator
content by hashes; no separate Git checkout, remote reference, or automated
fetch/update step is required.

The generated Python package exposes endpoint ownership through
`open_perception_kit.packet` and live C++ guest access through `open_perception_kit.guest`. The
generated internal Meson adapter is derived from the public SDK integration and
carries the same version requirements.

Run `./scripts/perception-sdk.sh package --output-dir artifacts` to create a
reproducible release archive containing the C++ SDK and integrations, open-perception-kit
and FlatBuffers Python wheels, schemas, and release manifest. See
[Build and use the Open Perception Kit bundle](../public/how-to/use-perception-sdk.md)
for the archive layout and consumer workflow.

Release packaging verifies the checked-in generation receipt and never invokes
the generator. Regeneration drift remains an explicit
`./scripts/perception-sdk.sh check` responsibility for development and CI.

All SDK operations use `./scripts/perception-sdk.sh` as their single command
surface.

See `schemas/perception/README.md` for schema versioning, FlatBuffers
compatibility rules, new payload creation, and the required validation
workflow.

## Runtime Envelope

`FrameResults` can contain multiple independent typed payloads. Producers append
payloads with the generated `add()` API, and consumers select known payload
families with `for_each<T>()`. This allows a frame to accumulate results across
cascades and independent elements without relying on layer ordering or a
hand-written variant container.

The currently generated payload roots are:

- `FrameContext` for video or audio frame geometry and crop/letterbox context
- `BoxDetections` for localized detections
- `Classifications` for classification candidates and person-presence output
- `PoseEstimations` for yaw and pitch estimates
- `SegmentationMasks` for bitmap-backed masks
- `ObjectEmbeddings` for embedding vectors
- `ObjectTracks` for tracker output and predicted detections
- `TrackTraces` for tracker history points
- `PerformanceOverlay` for displayable performance lines

Each payload family is versioned and identified independently. A consumer SDK
can preserve an unknown payload while forwarding or reserializing the envelope,
but typed access requires an SDK generated from a schema set that knows that
payload identity.

## Shared Metadata

Payloads that represent inference or processing output carry `LayerInfo`. It
records interpretation and provenance fields such as engine, model, tags,
producer element ID, label family, content type, and compositing mode. Consumers
must select payloads by type and semantic fields such as `content_type`, not by
their position in the envelope.

`LayerInfo.producer` identifies the component that created the payload rather
than only the enclosing inference element. Its `instance_id` distinguishes
multiple operations in one OpChain, `component` identifies the canonical Op or
element type, and `implementation` identifies the selected parser, script, or
processing implementation. Producer metadata is provenance and must not be
used as a replacement for payload-type or content-type routing.

Result items use `ObjectMeta` where identity or parent relationships are needed:

- `id` identifies an item within the producer's result model
- `parent_id` links derived output to its source item
- `creation_ts_ns` records the producer-provided creation timestamp

Frame geometry is represented by the `FrameContext` payload. Its video and audio
tables contain the original dimensions and the crop, letterbox, or sample-range
context needed to interpret downstream results.

## Runtime Transport

Inside GStreamer, `FrameResultsMeta` attaches a shared `FrameResults` instance to
the corresponding `GstBuffer`. `opkinfer` creates the metadata before OpChain
execution; postprocessors add generated payloads through
`OpChainContext::frameResults`; `opktracker` and `opkperformance` can append more
payloads; and `opkosd` reads supported payload types for visualization.

At application boundaries, the generated wire envelope is serialized as bytes.
`opkcomm` publishes those bytes in the `frame_results_packet_b64` field with the
`perception-frame-results+base64` encoding marker. External consumers must use a
compatible released Open Perception Kit SDK to decode and access the typed payloads.
The embedded `opksink` WebUI uses the generated TypeScript SDK at this boundary;
it validates producer identity and converts typed payloads into its established
OSD and output-panel presentation model.

## Testing Python Guest Scripts

When the development build enables tests, it provides the non-installed
`python_guest_script_executor` binary for testing trusted Python transformations
against a live C++ `FrameResults` envelope. The executor accepts an ordered list
of scripts and calls `process(env)` from each script against the same envelope:

```bash
./development/build/tests/python_guest_script_executor \
  --output /tmp/results.bin \
  seed_boxes.py \
  custom_postprocessor.py
```

Scripts import the generated guest type for annotations and append results with
the generated Python object API:

```python
from open_perception_kit.guest import Envelope


def process(env: Envelope) -> None:
    ...
```

Existing payloads are read-only through the bridge. A transformation therefore
reads its input payloads and appends new payloads rather than mutating entries in
place. After all scripts return successfully, the executor writes a raw
FrameResults packet that tests can decode with `open_perception_kit.packet.decode`.
`--python-path` can be repeated to add script dependencies to the embedded
interpreter's module search path. The embedded runtime uses the same Python
installation selected by Meson, including that installation's virtualenv
packages such as the generated SDK's FlatBuffers dependency.

The executor runs CPython in-process and is not a security sandbox. It is test
tooling only and does not add Python postprocessors to production OpChains.
