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

The structure diagram source is [custom-metadata-structure.puml](diagrams/custom-metadata-structure.puml).

## Relationship To Current Perception

Current architecture:

- `Perception` is the in-process C++ result container.
- `PerceptionMeta` carries that container with the media buffer.
- Built-in parsers write known result types into `Perception::Layer`.
- `pekosd`, `pekperformance`, and transport paths read or update the current
  `Perception` payload directly.

Future direction:

- `FrameMetadataEnvelope` becomes the transportable metadata envelope.
- Ordered `MetadataRecord` entries replace direct mutation of one shared payload.
- Built-in OPK results and custom user results use the same frame envelope.
- Custom payloads are opaque to OPK but identified by schema references.
- Rendering uses built-in and custom visual templates instead of hardcoded result
  types only.

This is not the checked-in runtime contract yet. It is intended to guide future
work across parsers, serializers, `pekinfer`, `pekosd`, `pekperformance`, and the
application-facing metadata transport.

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

The envelope is treated as immutable at the processing boundary. A stage reads
the current envelope and publishes a rebuilt envelope with its own contribution
rather than mutating existing producer payloads in place.

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

## Object Envelope And Custom Payloads

`ObjectEnvelope` is the common wrapper for built-in and custom objects. It should
carry the fields needed for identity, parent relationships, fallback rendering,
and optional custom decoding:

- `object_id`
- `parent_id`
- timestamp
- `type_name`
- label and confidence
- normalized bounding box
- attributes
- optional opaque payload bytes
- optional `schema_ref`

Opaque payloads are bytes transported by OPK but not interpreted by OPK. User-side
code decodes them using the schema identified by `schema_ref` and the generated
bindings known to that application.

## Schema References

`schema_ref` is a string beside the opaque payload, for example:

```text
schema_ref: "com.example.defect:1.0.0"
```

The payload itself is not self-described. Schema identity belongs beside the
payload so transport and renderer code can decide whether it knows how to decode
custom data.

Open schema policy questions remain, including whether `schema_ref` is mandatory
for every opaque payload and how schema versions should be discovered or cached.

## Postprocessing Integration

Built-in C++ postprocessing can emit built-in records. A future Python
postprocessing path could emit custom records with object envelopes, bounding
boxes, attributes, opaque payloads, and style overrides.

Python postprocessing is not available in the current runtime. Until it exists,
custom parsing remains C++-based through `GenericPostprocessOp`. See
[Known Limitations](known-limitations.md).

## Metadata Transport

The frame envelope should reach applications through a metadata transport boundary
instead of requiring renderers or applications to depend on GStreamer internals.

Conceptually:

```text
FrameMetadataEnvelope -> metadata transport -> renderer or application
```

Possible transports include WebSocket, file output, stdout, Redis, or another
application endpoint. The important boundary is that consumers depend on the
envelope contract, not on the transport implementation.

A standard versioned application endpoint does not exist yet. This is one of the
main future-development targets called out in [Known Limitations](known-limitations.md).

## Rendering And Visual Templates

Rendering should use a fixed primitive vocabulary and visual mappings. Built-in
OPK object types use provided mappings; custom object types can provide custom
mappings keyed by `type_name` and `schema_ref`.

The renderer resolves visualization using:

- built-in visual mappings
- optional custom visual mappings
- object `type_name`
- object fallback geometry
- style overrides

A mapping is a visual template, not just a type-to-primitive lookup. For example:

```yaml
types:
  builtin.face:
    primitives:
      - kind: circle
        bounds: { x: 0.05, y: 0.05, w: 0.90, h: 0.90 }
      - kind: text
        at: { x: 0.50, y: 1.05 }
        anchor: top-center
        text: "{label} {confidence}"
```

Coordinates are relative to the object's fallback geometry. Values outside
`0..1` can be used for labels or decorations placed outside the box.

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

Fallback rendering is required for custom object support. If no mapping is
available, the renderer should draw the object's fallback geometry, label, and
confidence using generic styling.

The current concept uses a normalized bounding box as the shared fallback
geometry. That is enough for many detection-like objects but does not cover all
current `Perception` result types. Segmentation maps, traces, gaze vectors,
embeddings, and text geometry need a separate mapping plan before this becomes a
complete replacement for current `Perception` rendering.

## Open Questions

- How should Python postprocessing snippets be registered, sandboxed, and tested?
- Should `schema_ref` be mandatory for every opaque payload?
- How should schema versions be discovered, cached, and validated?
- How expressive should visual template YAML become before it turns into a
  scripting language?
- Should style overrides be sparse patches over mapping defaults or full style
  replacements?
- Which current `Perception` result types need first-class future envelope fields
  versus schema-backed opaque payloads?
