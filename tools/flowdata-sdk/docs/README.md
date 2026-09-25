# SDK Usage Guides

These guides explain how to use the generated SDKs from application
code. Each page includes a short tutorial and an API reference:

Every generated language SDK in one release uses the same required stable
`--version MAJOR.MINOR.PATCH` input and exposes name-derived version constants.

- [C++ SDK Guide](cpp-sdk.md)
- [C++ Python Bridge Guest Script API](cpp-python-bridge.md)
- [Python SDK Guide](python-sdk.md)
- [Rust SDK Guide](rust-sdk.md)
- [TypeScript SDK Guide](typescript-sdk.md)
- [Integration Guide](integration-guide.md)

All SDKs use the same envelope model:

1. Payload schemas are generated into language-specific types.
2. An envelope carries ordered payload entries.
3. Each serialized entry has an internal payload id and an encoded payload blob.
4. Duplicate known payload types are valid; use the occurrence index when reading.
5. Known payload ids are generated from each root type and its schema dependency contents.
6. Readers decode entries whose ids and blobs match the current SDK and preserve unknown or incompatible entries; Rust additionally exposes their ids, bytes, and typed errors.
7. External payloads are explicit opaque byte entries addressed by deterministic external key handles; bare numeric external ids are not public API.
8. Invalid envelopes cannot be used for writing or serialization.

The C++ envelope keeps known payloads added through `add()` as native objects
until serialization, lazily caches native objects decoded from incoming packets,
and exposes read-only `payload_ref<T>` handles from `get<T>()`. C++ external
payloads use `external_key_t` overloads on `add/get/count/contains/for_each`.
The Python packet envelope and TypeScript envelope store encoded blobs. The
Python guest envelope instead borrows a live C++ container. Python endpoint
clients import `<sdk>.packet.Envelope`; embedded callbacks import
`<sdk>.guest.Envelope`. Both use the same generated payload classes and
`ExternalKey` handles.

The examples use illustrative demo payload root types:

- C++: `demo::perception::PerceptionT`, `demo::telemetry::TelemetryT`
- Python: `metapoc.fb.demo.perception.Perception.PerceptionT`, `metapoc.fb.demo.telemetry.Telemetry.TelemetryT`
- Rust: `metapoc::fb::demo::perception::PerceptionT`, `metapoc::fb::demo::telemetry::TelemetryT`
- TypeScript: `metapoc/fb/demo/perception/perception.js`, `metapoc/fb/demo/telemetry/telemetry.js`

The illustrative `payloads/common.fbs` defines reusable nested types such as
bounding boxes, positions, and vectors. It has no `root_type`, so it is generated
as shared support code rather than as an envelope payload.

Demo schemas, applications, and their test harness are not included here.
For the repository's supported Open Perception Kit workflow, use
[Use the Open Perception Kit SDK](../../../docs/public/how-to/use-perception-sdk.md).

In another project, the same API shape applies, but the generated namespaces,
classes, and imports follow that project's FlatBuffers schemas.

For agent handoff and repository operating rules, read [AGENTS.md](../../../AGENTS.md)
before making broad generator, API, or integration changes.
