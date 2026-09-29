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

# Heterogeneous Container

## Concept

A heterogeneous container is a transport object that can carry multiple payloads of different types in one serialized unit.

Instead of defining one monolithic schema that must contain every possible field up front, the container holds a sequence of independent payloads. Each payload keeps its own type identity and its own encoded bytes. The container itself only needs to answer three questions:

- what payloads are present
- how each payload is identified
- where the bytes for each payload are stored

This makes the container a small, stable envelope around a set of evolving payload definitions.

## Why use it

The main value of a heterogeneous container is decoupling.

- Producers can emit several logically separate data products together without flattening them into one large shared schema.
- Consumers can look for only the payloads they understand and ignore everything else.
- New payload types can be added without redesigning the outer transport shape.
- Independent teams can evolve payload schemas separately while sharing one common packet format.

In practice, this is useful when one message needs to carry mixed metadata such as perception output, telemetry, annotations, diagnostics, or future extensions that are not tightly coupled to each other.

## Core model

At the abstract level, the model is simple:

1. A container holds zero or more entries.
2. Each entry has a type identifier.
3. Each entry has a binary blob representing one typed payload instance.
4. A registry maps identifiers to payload definitions known by the current SDK.
5. Readers use that registry to decode matching known payloads.
6. Entries not known by the current SDK remain preserve-only blob entries.

The outer container does not need to understand the internal structure of each payload. It only preserves the pairing of:

- `type id`
- `encoded payload bytes`

That separation is what makes the container heterogeneous.

In this repository, known payload ids are generated internally from each
payload root type, its file identifier, and the contents of the FlatBuffers
schemas needed by that root. Source-tree paths are intentionally not part of
the identity. If a payload schema or one of its includes changes, that payload
gets a different id. Readers do not reject the whole packet for that mismatch:
they decode entries whose ids and blobs match the current SDK and preserve the
rest for forwarding. C++, Python, and TypeScript keep those entries private;
Rust also exposes read-only diagnostic views.

## Producer and consumer roles

The pattern usually has two sides.

### Producer side

A producer builds a container by inserting payload instances one by one:

1. Select the correct known payload type for each entry.
2. Append the payload instances to the container envelope.
3. Serialize the envelope for transport.

C++ and Rust retain appended native payloads until serialization. Python and
TypeScript encode known payloads when they are appended.

The producer does not need to know which consumers will read which payloads later.

### Consumer side

A consumer receives the container and first validates the outer envelope. It can then:

- test whether a specific payload is present
- count payload instances for a specific known type
- read a known payload by type and occurrence index

This allows partial understanding. A consumer can successfully process the container even if it only knows a subset of the payload types inside it.

## Design properties

### Extensibility

Adding a new payload type usually means adding a new schema and a new registry entry, not changing the outer container format.

### Partial compatibility

Older readers can skip newer or changed payloads they do not recognize. Newer
readers can continue to read older packets for payload roots whose generated
ids still match.

### Separation of concerns

The transport envelope remains stable, while payload schemas evolve independently.

### Mixed granularity

One packet can carry coarse-grained data products together even when they have very different shapes, sizes, and update cadences.

## Versioning and forwarding

The basic heterogeneous-container model is useful on its own, but production use often needs stronger lifecycle and interoperability guarantees.

### Artifact versioning and release governance

Generated SDK artifacts receive one explicit stable semantic version supplied
to the generator. The same release version is exposed by the C++, Python, Rust,
and TypeScript APIs and their package/build metadata.

A robust container ecosystem still needs a release process for both:

- the outer container API
- the payload schemas carried inside it

That process should define:

- clear compatibility rules for additive vs breaking changes
- how major, minor, and patch releases correspond to schema and API evolution
- a documented process for introducing, deprecating, and removing payload definitions
- validation or CI checks that prevent accidental wire-format drift

The generator validates and propagates the caller-selected version but does not
compare releases or automatically enforce the correct bump. Consuming projects
remain responsible for that release policy and CI governance.

### Unknown payload forwarding

Forwarding, bridging, gateway, and mixed-version scenarios may require a
component to carry entries it cannot decode. In this repository, unknown
low-domain generated ids are preserve-only and are serialized again when the
envelope is forwarded. Explicit external payloads are caller-owned opaque blobs
addressed by key handles created with `external_key(...)`, not bare strings or
numeric ids.

## Tradeoffs

This pattern is not free.

- Consumers need a registry that maps identifiers to payload definitions.
- Payload discovery happens at runtime, not purely from the outer type system.
- Duplicate known payload types are valid because types are tags, not unique keys.
- Payload entries are ordered and append-only in the generated APIs.
- Unknown generated-domain payloads can be preserved for forwarding, while custom
  opaque payload insertion uses the explicit external payload API.

The design works best when the envelope is intentionally small and stable, and when payload ownership is well defined.

## Relation to this repository

In this repository, the heterogeneous container is the conceptual center of the generated SDKs.

The payload schemas under `payloads/` define the typed data units. The generated code provides one envelope abstraction around ordered payload entries:

- appending typed payload instances
- reading one or more payload instances by known payload type

The repository uses an envelope made of repeated payload entries, where each
entry contains an identifier and a blob. Public APIs expose append and read
operations rather than treating the envelope as a mutable global map: duplicate
ids are valid, entries are ordered, and erase, edit, or upsert operations are
intentionally not exposed. Compatibility is checked per payload id, so a reader
can decode the entries that match its SDK, preserve incompatible known-domain
entries for forwarding, and explicitly transport external opaque blobs
by key handle. The concrete wire representation is generated
code, but the higher-level concept is language independent.

## Language bindings in context

The repository includes concrete bindings for several environments, but they all implement the same conceptual model.

- C++: generated SDK plus CMake and Meson integration; both build the same `demo/cpp/client.cpp` demo as `client`
- Python: generated SDK used both directly and as a helper for producing demo packets
- Rust: generated owning crate with typed payload access and read-only entry diagnostics
- TypeScript: generated envelope reader/writer SDK plus the npm demo consumer

These are bindings around the same heterogeneous container idea, not separate container designs. The language-specific APIs differ, but the shared abstraction stays the same: a stable envelope carrying multiple independently typed payloads.
