---
sidebar_position: 10
sidebar_label: Perception
---

# Perception

`Perception` is the persistent metadata container that travels downstream with a
media buffer. It aggregates structured results from inference, postprocessing,
tracking, and performance elements.

## Generated SDKs

The canonical schema and generated output directories are declared in
`tools/perception/sdk.json`. Run `./scripts/perception-sdk.sh generate` to
regenerate the checked-in C++ and Python SDKs; use
`./scripts/perception-sdk.sh check` in CI to detect drift.

`tools/perception/sdk.json` is the only hand-edited SDK release descriptor. It
defines the SDK identity, canonical schema and generated directories,
flowdata-sdk location, generated project integrations, and checksum-locked
FlatBuffers and Python wheel-build artifacts. `tools/perception/sdk_config.py` is the shared loader
used by generation, packaging, tests, and development installation. Generation first verifies the raw
flowdata manifests, then applies AMP-owned copyright and formatting decoration. The final
The generated SDK manifest embeds the raw generator manifests and records the
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

![Inference Data Collection (Perception)](../public/static/img/perception.png)

The model supports multi-stage inference, branching pipelines, UUID-based
cross-stage references, and backend-agnostic result representation.

## Object Base

`Perception::Object` is the common base for stored entities. It provides:

- `uuid` for stable entity identity
- `parentUuid` for relationships such as frame -> detection -> derived result
- `creationTsNs` for correlation and ordering

These fields allow pipeline stages to link results without relying on array
positions.

## Frame Anchors

`VideoFrame` describes a video frame and its geometry. It acts as the root object
for vision detections and stores dimensions and crop/letterbox data needed for
coordinate mapping.

`AudioFrame` describes an audio chunk and acts as the root object for audio
results when audio inference is present.

## Detection Types

`Perception` provides normalized result types for common outputs:

- `Rect` for localized detections
- `Classification` for top-k candidates
- `YawPitch` for angular or regression outputs
- `LocalizedText` for OCR-style text payloads
- `SegmentationMap` for dense pixel outputs
- `TrackTrace` for tracker history
- `ObjectEmbedding` for embedding or ReID vectors

Detections are stored as a tagged variant, so a layer can contain heterogeneous
result types while remaining type-safe.

## Layer

`Perception::Layer` represents the result of one inference or processing step.
Layer metadata records provenance and interpretation context, including engine,
model, tags, producer element ID, label family, and content type.

`detections` contains the structured outputs produced by that step. Multiple
layers can accumulate as a buffer moves through cascades, parallel branches, or
postprocessing elements.

## Performance Data

`perfdata` stores lightweight performance strings. `pekperformance` writes these
values and `pekosd` can render them as an overlay.
