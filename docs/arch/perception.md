---
sidebar_position: 10
sidebar_label: Perception
---

# Perception

Perception is the schema and SDK domain for structured runtime results.
`perception::FrameResults` is the concrete C++ runtime container: a generated,
typed envelope that travels downstream with a media buffer and accumulates
payloads from postprocessing, tracking, and performance elements.

The naming boundary is intentional. **Perception** identifies the schema set,
generated SDK, package, and namespace; **FrameResults** identifies one frame's
runtime result envelope.

## Generated SDKs

The canonical schema and generated output directories are declared in
`tools/perception/sdk.json`. Run `./scripts/perception-sdk.sh generate` to
regenerate the checked-in C++ and Python SDKs; use
`./scripts/perception-sdk.sh check` in CI to detect drift.

Development generation and release packaging are intentionally separate.
`$regenerate-perception-sdk` updates tracked generated sources during
implementation. `$package-perception-sdk-release` consumes an already committed
snapshot and creates distributable artifacts without regenerating it.

`tools/perception/sdk.json` is the only hand-edited SDK release descriptor. It
defines the SDK identity, canonical schema and generated directories,
flowdata-sdk location, generated project integrations, and checksum-locked
FlatBuffers and Python wheel-build artifacts. `tools/perception/sdk_config.py` is the shared loader
used by generation, packaging, tests, and development installation. Generation first verifies the raw
flowdata manifests, then applies AMP-owned copyright and formatting decoration. The final
generated SDK manifest embeds the raw generator manifests and records the
descriptor hash, decorated file hashes, and derived project integrations.

The generated Python package exposes endpoint ownership through
`perception.packet` and live C++ guest access through `perception.guest`. The
generated internal Meson adapter is derived from the public SDK integration and
carries the same version requirements.

Run `./scripts/perception-sdk.sh package --output-dir artifacts` to create a
reproducible release archive containing the C++ SDK and integrations, Perception
and FlatBuffers Python wheels, schemas, and release manifest. See
[Build and use the Perception SDK bundle](../public/how-to/use-perception-sdk.md)
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

Result items use `ObjectMeta` where identity or parent relationships are needed:

- `id` identifies an item within the producer's result model
- `parent_id` links derived output to its source item
- `creation_ts_ns` records the producer-provided creation timestamp

Frame geometry is represented by the `FrameContext` payload. Its video and audio
tables contain the original dimensions and the crop, letterbox, or sample-range
context needed to interpret downstream results.

## Runtime Transport

Inside GStreamer, `FrameResultsMeta` attaches a shared `FrameResults` instance to
the corresponding `GstBuffer`. `pekinfer` creates the metadata before OpChain
execution; postprocessors add generated payloads through
`OpChainContext::frameResults`; `pektracker` and `pekperformance` can append more
payloads; and `pekosd` reads supported payload types for visualization.

At application boundaries, the generated wire envelope is serialized as bytes.
`pekcomm` publishes those bytes in the `frame_results_packet_b64` field with the
`perception-frame-results+base64` encoding marker. External consumers must use a
compatible released Perception SDK to decode and access the typed payloads.
