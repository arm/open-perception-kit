---
name: integrate-perception-sdk-consumer
description: Integrate external C++, Python, Rust, or TypeScript applications with a released Open Perception Kit package and serialized FrameResults packets. Use for consumer-side proof of concepts, Plumber-like decoders, browser clients, Cairn adapters, build integration, package installation, producer identity and compatibility checks, payload routing, or SDK upgrade work. Do not use this skill to modify schemas, regenerate checked-in SDK sources, or create release bundles.
---

# Integrate an Open Perception Kit consumer

Build consumers against a verified released SDK, validate the producer identity
before typed access, and keep transport handling separate from application
semantics.

## Establish the Consumer Boundary

1. Read `docs/public/how-to/use-perception-sdk.md` and
   `docs/arch/perception.md` when working in the OPK repository.
2. Inspect the received bundle's `open-perception-kit-release-manifest.json` and
   `metadata/open-perception-kit-manifest.json`.
3. Identify how results arrive:
   - an `opkcomm` JSON or NDJSON record
   - raw serialized envelope bytes
   - an embedded Python guest bridge
   - an application-specific transport such as a Cairn service
4. Identify which payload types and `LayerInfo.content_type` values the
   application actually needs.
5. Define whether the application is a strict typed consumer or a transparent
   relay that must preserve unknown payloads.

Do not regenerate an SDK in a consumer project. External consumers should use a
released bundle. If the released SDK does not expose a required payload through
its typed API, obtain a matching newer SDK release.

## Verify and Install the SDK

Prefer the ZIP, checksum, and provenance sidecar as one release unit. When the
producer repository is available, verify them with:

```bash
./scripts/perception-sdk.sh verify \
  /path/to/open-perception-kit-<MAJOR.MINOR.PATCH>.zip \
  --require-sidecars
```

After extraction, install Python dependencies only from the bundle:

```bash
python3 -m pip install \
  --no-index \
  --find-links python \
  -r python/requirements.txt
```

Record the SDK version, schema-set SHA-256, FlatBuffers version, archive
SHA-256, and source commit in the consuming project or deployment metadata.

## Integrate Python Consumers

Use `open_perception_kit.packet.Envelope` or `open_perception_kit.packet.decode` for serialized
packets. Follow the checked-in Plumber pattern in
`tools/plumber/plumber/frame_results_decode.py` when available:

1. Require `frame_results_encoding == "perception-frame-results+base64"`.
2. Decode Base64 strictly and reject missing or malformed values.
3. Construct or decode the envelope and require `valid()`.
4. Check `producer_identity()` before typed payload access.
5. Iterate known generated native types with `for_each(TypeT)`.
6. Route payloads by generated type and semantic metadata, not envelope order.

Import `open_perception_kit.guest` only inside a C++ host that registers the generated
`open_perception_kit_bridge` module. A normal standalone Python process must use
`open_perception_kit.packet`; the guest module intentionally fails outside its host.

## Integrate C++ Consumers

Vendor the complete `cpp/` directory from the release bundle. Do not copy
individual generated headers or depend on OPK-internal headers under
`development/common/`.

- **CMake:** include `cpp/cmake/open_perception_kit.cmake`, call
  `open_perception_kit_enable_sdk()`, and link `open_perception_kit::sdk`.
- **Meson:** call `subdir('path/to/cpp/meson/open_perception_kit')` and use
  `open_perception_kit_dep`.
- **Embedded Python:** use the generated bridge integration in addition to the
  core SDK integration.

Use C++20 and the exact compatible FlatBuffers headers required by the bundle.
Construct `open_perception_kit::container::envelope` from packet bytes, require
`valid()`, check `producer_identity()`, then use `for_each<T>()` for known
generated payload types. The `open_perception_kit::FrameResults` name used inside OPK is
an internal convenience alias and is not the external SDK contract.

## Integrate TypeScript Consumers

Install the open-perception-kit and FlatBuffers npm-compatible tarballs from the release
bundle. Import `Envelope` and generated payload classes from `open-perception-kit`,
require a valid envelope and exact producer identity, then iterate typed payloads
with `for_each(TypeT)`. Browser applications should bundle the SDK and runtime
rather than serving unresolved npm imports directly.

## Integrate Rust Consumers

Add the extracted `rust/` directory as a path dependency. Import `Envelope`,
`payload`, and generated native payload types from `open_perception_kit`. Require
successful `Envelope::decode(...)` and an exact `producer_identity()` match
before typed access. Reuse a typed selector with `count`, `contains`, `get`, and `for_each`; use
`external_key` for external payloads. Unknown, changed, and malformed entries
remain available through `entries()` and are preserved when reserialized.
For offline builds, copy the bundled Cargo source-replacement configuration
into the consumer workspace and point it at the extracted `rust/vendor`
directory. Run `cargo generate-lockfile --offline` once in the consumer
workspace before building with `cargo build --offline --locked`.

## Apply Compatibility Policy

Use producer identity as the default typed-access gate:

- `exact_match`: permit typed processing.
- SDK name, version, or schema-set mismatch: fail with the received and expected
  identities unless the application has explicit mixed-version fixture tests.
- missing or malformed identity: reject typed processing.

An older SDK can retain unknown payload blobs while decoding and reserializing
an envelope, but it cannot interpret those payloads through its generated typed
API. A relay may preserve the original packet or reserialize the envelope; a
semantic consumer must use a matching SDK.

Do not infer compatibility from payload `schema_major` and `schema_minor`
fields alone. Use the released SDK version, schema-set digest, generated payload
identities, and tested application semantics.

## Consume Payload Semantics Safely

- Select payloads by generated type and fields such as `content_type`, not by
  array or envelope position.
- Treat generated table pointers and optional nested objects as nullable.
- Use `FrameContext` for coordinate-space and crop or letterbox interpretation.
- Use `ObjectMeta.id` and `parent_id` only according to the producer's declared
  relationship semantics.
- Keep tracker IDs, application IDs, and object IDs distinct.
- Define behavior for absent, repeated, unsupported, and malformed payloads.
- Keep visualization or business assumptions in the consuming application,
  rather than treating them as schema guarantees.

## Integrate Through Cairn

Inspect the actual Cairn SDK and service contract before editing; do not assume
that OPK already provides a Cairn FrameResults adapter. Prefer a narrow boundary
that transports the encoding marker, serialized packet bytes, frame identifier,
and producer identity without converting every payload into an unrelated JSON
model.

Decode at the component that owns perception semantics. If an intermediate
Cairn service is only a relay, preserve the packet bytes and compatibility
metadata unchanged. If Cairn requires typed messages, document the mapping,
loss behavior, version negotiation, and ownership of SDK upgrades.

## Validate the Consumer

Add focused tests for:

- successful decode of a known packet from the selected SDK release
- malformed Base64 and invalid FlatBuffers envelopes
- every producer identity mismatch status
- required payload missing and repeated-payload behavior
- semantic routing by payload type and `content_type`
- unknown-payload preservation when the application acts as a relay
- CMake or Meson configuration against the extracted C++ tree
- Python installation from the bundle without network access
- TypeScript installation from the bundle without registry access

Use captured packets or deterministic fixtures from the producer release. Do
not generate test packets with a different SDK version and call them compatible.

## Hand Off Producer-Side Work

If integration reveals that a schema or generated SDK change is required, stop
consumer work and hand off appropriately:

- use `$evolve-perception-schema` for schema design and compatibility decisions
- use `$regenerate-perception-sdk` for checked-in generated source updates
- use `$package-open-perception-kit-release` for a new verified release bundle

## Report

State the consumer language and build system, SDK source and version, transport
contract, producer identity policy, consumed payloads, unknown-payload behavior,
tests run, Cairn mapping if applicable, and any required producer-side change.
