---
sidebar_position: 11
sidebar_label: Future Metadata
---

# Future Metadata Architecture

This page describes a future development phase for OPK metadata. The current
runtime contract is still `Perception` carried by `PerceptionMeta`; this proposal
shows how that contract could evolve into a FlatBuffers-native frame metadata
envelope that supports built-in outputs and user-defined custom outputs without
requiring code changes for every new object type.

The structure diagram source is [custom-metadata-structure.puml](../plantuml/custom-metadata-structure.puml).

## Relationship To Current Perception

Current architecture:

- `Perception` is the in-process C++ result container.
- `PerceptionMeta` carries that container with the media buffer.
- Built-in parsers write known result types into `Perception::Layer`.
- `pekosd`, `pekperformance`, and transport paths read or update the current
  `Perception` payload directly.

Future direction:

- `FrameMetadataEnvelope` becomes the metadata payload carried inside the pipeline.
- GstBuffer-attached metadata remains the internal GStreamer transport mechanism.
- Ordered `MetadataRecord` entries replace direct mutation of one shared payload.
- Objects form a graph through `parent_id`.
- Object nodes carry a typed payload, such as generic object data, bbox,
  segmentation, trace, embedding, classification, text, vector, or custom bytes.
- Custom payloads are opaque to OPK but identified by schema references.
- `pekcomm` exposes the envelope to user-side applications and renderers.
- Rendering follows the object graph and emits primitives from renderable nodes.

This is not the checked-in runtime contract yet. It is intended to guide future
work across parsers, serializers, `pekinfer`, `pekosd`, `pekperformance`,
GstBuffer metadata attachment, `pekcomm`, and the application-facing metadata
transport.

## Frame Metadata Envelope

`FrameMetadataEnvelope` is the proposed top-level container for one frame or
processing unit. It carries unique frame data plus an ordered list of metadata
records.

Frame data is stored once and is not repeated by every producer record. Typical
frame fields include:

- `frame_id`
- timestamp
- frame width and height
- source identity

The envelope payload is treated as immutable at the processing boundary. A stage
reads the current envelope and publishes a rebuilt envelope with its own
contribution rather than mutating existing producer payloads in place. Inside the
GStreamer pipeline, that rebuilt envelope is still attached to the `GstBuffer` as
metadata so existing element-to-element flow remains buffer-based.

## Metadata Records

A `MetadataRecord` is one producer's contribution to the frame. Each record can
contain objects, style overrides, and performance items.

A record carries:

- `record_id`
- `record_sequence`
- `producer_element`
- `record_type`
- `objects`
- `style_overrides`
- `perf_items`

`record_sequence` is assigned by the metadata aggregator so producers fold into a
deterministic order, even when records are created by different threads. Object
order inside a record is not semantically meaningful. Style override order inside
a record is meaningful and follows producer-defined order.

## Object Graph And Payload Layers

`ObjectEnvelope` is a graph node. It contains identity and relationship fields,
then delegates type-specific data to a typed payload.

Common envelope fields:

- `object_id`
- `parent_id`
- timestamp
- `type_name`
- typed payload

`parent_id` is the composition mechanism. It can represent crop/result lineage,
embeddings attached to detections, user-defined extensions attached to built-in
objects, or renderable geometry attached to a semantic object.

Example object stack:

```text
genericObject
├── bbox
├── classification
├── embedding
└── custom
```

The parent `genericObject` can carry semantic label/confidence data, while child
nodes carry renderable geometry, derived classifications, embeddings, or custom
application payloads.

## Built-In Payloads And Custom Payloads

Built-in payloads carry OPK-known structure. The initial payload set should stay
small and cover current architectural needs:

- `GenericObjectPayload`: label, confidence, and common attributes.
- `BBoxPayload`: normalized rectangle and coordinate-space metadata.
- `SegmentationPayload`: mask reference or mask bytes, dimensions, placement, and
  rendering hints.
- `TracePayload`: ordered points for tracks or trails.
- `EmbeddingPayload`: vector data attached to a parent object.
- `ClassificationPayload`: label candidates and confidence values.
- `TextPayload`: text payload and optional placement.
- `VectorPayload`: origin and direction or endpoint.
- `CustomPayload`: opaque bytes plus schema identity.

Custom data should normally be emitted as a child object with a `CustomPayload`,
not embedded into a built-in payload. This keeps built-in objects valid and
renderable without understanding user-defined schemas.

## Schema References

`schema_ref` identifies how to decode a `CustomPayload`, for example:

```text
schema_ref: "com.example.defect:1.0.0"
```

The custom payload itself is not self-described. Schema identity belongs beside
the opaque bytes so user-side code can decide whether it knows how to decode the
extension.

Open schema policy questions remain, including whether `schema_ref` is mandatory
for every custom payload and how schema versions should be discovered or cached.

## Postprocessing Integration

Built-in C++ postprocessing can emit built-in object payloads. A future Python
postprocessing path could emit custom child objects with `CustomPayload`, or emit
built-in payloads when it wants OPK default rendering behavior.

Python postprocessing is not available in the current runtime. Until it exists,
custom parsing remains C++-based through `GenericPostprocessOp`. See
[Known Limitations](known-limitations.md).

## Metadata Transport

The transport model is hybrid. Inside the GStreamer pipeline, the frame envelope
continues to travel as metadata attached to the `GstBuffer`. That keeps OPK
elements aligned with the current buffer-based execution model.

At the application boundary, `pekcomm` publishes the same envelope to user-side
applications and renderers. Consumers should depend on the envelope contract, not
on GStreamer internals.

Conceptually:

```text
GstBuffer metadata carrying FrameMetadataEnvelope
  -> pekcomm
  -> user-side renderer or application
```

`pekcomm` may later expose the envelope through WebSocket, file output, stdout,
Redis, or another endpoint. A standard versioned application endpoint does not
exist yet; this is one of the main future-development targets called out in
[Known Limitations](known-limitations.md).

## Graph-Driven Rendering

Rendering follows the object graph. `ObjectEnvelope` nodes are visited in
parent-child order. Nodes with known renderable payloads emit primitives. Nodes
can use limited context from their parent or same-parent siblings for labels,
anchors, and style.

Semantic nodes such as `genericObject` do not need to render directly. They become
visible through renderable child nodes such as `bbox`, `segmentation`, `trace`,
or `text`.

The renderer should support a small fixed primitive vocabulary, such as:

- point
- line
- polyline
- polygon
- rect
- mask
- text

Built-in payloads can have default renderers. Custom payloads render only when a
custom graph rule or renderer extension exists.

## Render Mapping Rules

Visual mappings attach behavior to node types or payload kinds, not to one whole
semantic object. A rule can emit primitives from `self` and can read limited graph
context:

- `self`
- `parent`
- `children[type_name]`
- `sibling[type_name]`

Example:

```yaml
renderers:
  bbox:
    emits:
      - kind: rect
        bounds: self.payload.bbox
        label: parent.payload.label
        confidence: parent.payload.confidence

  segmentation:
    emits:
      - kind: mask
        mask: self.payload.mask_ref
        placement: self.payload.placement

  classification:
    emits:
      - kind: text
        text: self.payload.label
        anchor:
          from: sibling.bbox
          at: bottom-left
```

The mapping language should stay constrained. It should follow the graph without
becoming a general query language or scripting runtime.

## Style Overrides

Style is emitted separately from object payloads. A `StyleOverride` targets a
record or object by id:

- `target_kind`: record or object
- `target_id`: record id or object id

Suggested ordering rules:

1. Fold records by `record_sequence`.
2. Apply style overrides inside a record in vector order.
3. Let object-level overrides take precedence over record-level overrides.

Typical style fields include stroke color, fill color, text color, stroke width,
point size, opacity, and line style.

## Fallback Rendering

Fallback rendering is intentionally simple. If no custom mapping exists, the
renderer can still draw built-in renderable payloads using their default behavior:

- `BBoxPayload` draws a rectangle.
- `SegmentationPayload` draws a mask if the mask data or reference is available.
- `TracePayload` draws a polyline.
- `TextPayload` draws text.
- `VectorPayload` draws a line or arrow.
- `GenericObjectPayload`, `EmbeddingPayload`, and `CustomPayload` do not draw by
  themselves.

Objects without renderable payloads remain valid metadata and should still be
available to metadata inspection or application logic.

## Open Questions

- How should Python postprocessing snippets be registered, sandboxed, and tested?
- Should `schema_ref` be mandatory for every `CustomPayload`?
- How should schema versions be discovered, cached, and validated?
- How expressive should visual mapping YAML become before it turns into a
  scripting language?
- Should style overrides be sparse patches over mapping defaults or full style
  replacements?
- Which current `Perception` result types need built-in payloads first, and which
  can initially remain custom or non-renderable payloads?
