---
sidebar_position: 10
sidebar_label: Perception
---

# Perception

`Perception` is the persistent metadata container that travels downstream with a
media buffer. It aggregates structured results from inference, postprocessing,
tracking, and performance elements.

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
