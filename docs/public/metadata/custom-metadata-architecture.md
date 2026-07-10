---
title: Custom Metadata Architecture
sidebar_position: 1
sidebar_label: Custom Metadata Architecture
description: Concept sketch for a FlatBuffers-native metadata container, object groups, and template-driven visualization.
---

# Custom Metadata Architecture

This note captures the current concept for a metadata system that supports built-in perception outputs and user-defined custom outputs without requiring codebase changes for each new object type.

The matching structure diagram is [custom-metadata-structure.puml](diagrams/custom-metadata-structure.puml).

This is a concept note, not a final implementation contract.

## Goals

This concept is guided by these design decisions:

- The internal metadata container is FlatBuffers-native.
- The frame envelope carries object groups, not direct render primitives.
- Metadata is treated as immutable: processing stages read the current envelope and publish a rebuilt envelope rather than mutating payloads in place.
- Built-in object data and custom object data are carried in the same frame envelope.
- User-provided Python postprocessing snippets can create custom object groups.
- Custom payloads are opaque bytes inside OPK.
- Schema identity is a simple object-level string reference beside the opaque payload.
- A normalized bounding box is the common render geometry.
- Style overrides are separate targetable elements and can attach to either a group or an object by id.
- Built-in schema definitions are explicit user-side artifacts.
- Metadata reaches the renderer through a transport interface.
- Rendering is driven by built-in plus custom visual template mappings.
- The renderer keeps a fixed primitive vocabulary; composite types decompose into those primitives.

## Runtime Container

The internal metadata container is a `FrameEnvelope`.

Each processing stage follows the same immutable update pattern:

1. read the current frame metadata envelope
2. decode the parts it needs
3. append its own output

## Groups

A group is a producer-defined collection of logically linked objects. 

Each `Group` contains:

- `group_id`
- `group_name`
- `producer_element`
- `objects`

## Object Envelope

Each object is carried as an `ObjectEnvelope`.

The object envelope contains:

- object identity, such as `object_id`
- `parent_id`
- `timestamp`
- `type_name`
- `label`
- `bbox`
- `confidence`
- attributes
- optional opaque payload
- optional `schema_ref` string describing how to decode that payload

The bounding box is the only normalized geometry currently modeled. That keeps the internal container simple and gives the renderer a stable fallback.

## Opaque Payload

Custom payloads are internally transparent to OPK.

`OpaquePayload` is just bytes:

```text
OpaquePayload
└── payload: [ubyte]
```

OPK transports this payload but does not interpret it. Interpretation belongs to the user side, using the object's `schema_ref` and the user-provided generated bindings.

## Schema References

`schema_ref` is a simple string on `ObjectEnvelope`.

It connects an opaque payload to the schema or binding known by the user side, for example:

```text
schema_ref: "com.example.defect:1.0.0"
```

The important constraint is that the opaque payload itself is not self-described. Schema identity belongs beside the payload, not inside it.

## Postprocessing

Built-in postprocessing and injected Python postprocessing are executable/runtime elements, not data structures.

Built-in postprocessing emits built-in object groups.

Injected Python postprocessing is provided by the user as a snippet and emits custom object groups. The snippet can:

- create object envelopes
- set object `type_name`
- populate the bounding box
- emit opaque payload bytes
- emit style overrides

## Style Overrides

Style is not embedded in an object.

`StyleOverride` is a separate element in the frame envelope. It references its target by id:

- `target_kind`: group or object
- `target_id`: group id or object id

This allows one style override to apply to a whole group, while still allowing object-level overrides where needed.

Typical style fields include:

- stroke color
- fill color
- text color
- stroke width
- point size
- opacity
- line style

## Metadata Transport

The frame envelope is published through a metadata transport interface before it reaches the user-side renderer.

The diagram currently models this as:

```text
FrameEnvelope -> Metadata transport -> Renderer
```

The transport can later map to Redis, WebSocket, file output, or another delivery mechanism. The renderer should depend on the envelope contract, not on the transport implementation.

## User-Side Rendering

User-side rendering should use the provided API to access and decode built-in metadata types. The built-in visualization mapping is also part of that provided API.

For custom payloads, the user side incorporates the custom schema associated with each object `schema_ref` and uses it to decode the opaque payload. The user side can also provide a custom visualization mapping for custom object types.

The renderer resolves visualization using:

- the built-in visualization mapping from the provided API
- optional custom visualization mappings
- object `type_name`
- object `bbox`
- style overrides

## Type Mapping

The mapping is better thought of as a visual template, not just a type-to-primitive lookup.

Because the object envelope currently exposes only a bounding box, a mapping must describe how to place primitives inside that box.

Example:

```yaml
types:
  builtin.face:
    primitives:
      - kind: circle
        bounds: { x: 0.05, y: 0.05, w: 0.90, h: 0.90 }
      - kind: point
        at: { x: 0.50, y: 0.50 }
      - kind: text
        at: { x: 0.50, y: 1.05 }
        anchor: top-center
        text: "{label} {confidence}"
```

Coordinates are relative to the object bounding box. Values outside `0..1` can be used for labels or decorations placed outside the box.

## Renderer Primitive Vocabulary

The renderer keeps a fixed primitive vocabulary:

- point
- line
- polyline
- polygon
- rect
- circle
- text

Built-in and custom composite types must decompose into this primitive set through the mapping files. Since `bbox` is the only shared geometry field, non-rectangular primitives are placed using bbox-relative coordinates supplied by the visual template.

## Fallback Rendering

Fallback behavior is required for custom object support.

If no custom visualization mapping is available for a custom object type, the renderer should use the generic bounding-box visualization. That fallback draws the object bounding box and label using the object envelope fields.

With the current object model, this is the primary fallback because every renderable object is expected to provide a bounding box. Other primitives are still available to visual templates, but their coordinates are derived from bbox-relative placement rules rather than extra object geometry fields.

## Container Shape Sketch

```text
FrameEnvelope
├── frame_info
├── groups[]
│   ├── Group
│   │   ├── group_id
│   │   ├── group_name
│   │   ├── producer_element
│   │   ├── objects[]
│   │   │   ├── ObjectEnvelope
│   │   │   │   ├── object_id / parent_id
│   │   │   │   ├── type_name
│   │   │   │   ├── label / confidence / attrs
│   │   │   │   ├── bbox
│   │   │   │   ├── opaque_payload
│   │   │   │   └── schema_ref
├── perf_data[]
└── style_overrides[]
```

## Open Questions

- how the Python postprocessing snippet is registered and sandboxed
- whether `schema_ref` should be mandatory for every object with an opaque payload
- how expressive the visual template YAML should become before it is too close to a scripting language
- whether style overrides should be full replacements or sparse overrides over mapping defaults
