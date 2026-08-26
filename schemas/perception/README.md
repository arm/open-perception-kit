# Perception Schema Evolution Workflow

This directory contains the FlatBuffers schemas used to generate the Perception
C++, Python, and TypeScript SDKs. This document defines how to add or evolve schemas, choose
versions, evaluate compatibility, regenerate the SDKs, and validate a change.

## Sources of Truth

- `schemas/perception/metadata/` contains the complete authored schema set.
- `tools/perception/sdk.json` owns the SDK name, schema and
  output paths, FlowData generator location, exact FlatBuffers version, and
  release-tool checksums.
- `development/meson.build` owns the shared PEK and SDK package version.
- `generated/perception/` contains derived C++, Python, and TypeScript SDK output.
- `generated/perception/perception-sdk-manifest.json` is the generation receipt.
- `scripts/perception-sdk.sh` is the supported command surface for generation,
  drift checking, packaging, verification, and development installation.
- `tools/perception/evaluate_schema_change.py` evaluates schema compatibility
  and required PEK release impact against a Git revision.

Do not edit generated files, generated build integrations, or manifests by
hand. Change the schemas or `tools/perception/sdk.json`, then regenerate them.

## Workflow Boundaries

Perception SDK work has three separate concerns:

1. Use `$evolve-perception-schema` to design or change authored schemas, assess
   compatibility, record the required PEK release impact, and update runtime semantics.
2. Use `$regenerate-perception-sdk` during implementation to materialize and
   validate the tracked C++, Python, and TypeScript SDK snapshot. Commit these generated
   files normally with their authored inputs.
3. Use `$package-perception-sdk-release` only from a committed release snapshot
   to create and verify the distributable ZIP and sidecars. Packaging never
   regenerates checked-in files.

Do not use release packaging to obtain generated sources, and do not create a
release bundle merely to validate implementation drift.

## Compatibility Model

Compatibility has three distinct layers:

1. **FlatBuffers wire compatibility** determines whether old and new schema
   definitions can interpret the same payload bytes.
2. **Generated payload identity** determines whether an SDK recognizes a blob
   as a known typed payload. The FlowData generator derives the numeric payload
   ID from the qualified root type, four-character file identifier, and the
   content of the root schema and all its dependencies. Any relevant schema
   change therefore produces a new numeric payload ID.
3. **SDK API compatibility** covers generated C++, Python, and TypeScript names, fields,
   types, build requirements, and runtime semantics.

An additive FlatBuffers change may be wire-compatible while still producing a
new generated payload ID and generated API. An older SDK can retain an unknown
payload blob in a serialized envelope, but it cannot access that revision
through its generated typed API. Consumers that need typed access must update
to an SDK generated from the new schema set.

The generation manifest records the complete schema-set SHA-256, every payload
identity, the SDK version, the generator version, the exact FlatBuffers
identity, and hashes for all generated files. Packaging verifies this receipt
and does not regenerate the SDK.

## Versioning Policy

SDK packages use the PEK product version from `development/meson.build`. Apply
the schema compatibility impact to that release version:

- Increment **MAJOR** for an incompatible generated API or semantic contract,
  including removed or renamed public fields, changed field types, changed
  meanings, or removed payload roots.
- Increment **MINOR** for backward-compatible additions, including a new payload
  root or an optional field appended to an existing table. Consumers still need
  the new SDK version for typed access to the new payload identity.
- Increment **PATCH** for compatible generator, packaging, documentation, or
  implementation fixes that do not change the public schema contract. If a
  generator update changes the public generated API, version according to that
  API impact instead.

Each transportable root currently includes `schema_major` and `schema_minor`
fields initialized to `1.0`. Treat these values as payload-level format markers,
not as a substitute for the SDK version or generated payload identity. Changing
a FlatBuffers default can cause old bytes to be interpreted using the new
default, so do not bump these defaults as a routine versioning mechanism. A
proposal to use them for negotiation must first define explicit serialization,
reader behavior, and mixed-version tests.

Every new transportable root must have:

- one `root_type`
- one unique four-character ASCII `file_identifier`
- initial `schema_major:ushort = 1` and `schema_minor:ushort = 0` fields
- a namespace under `perception.metadata`

Never reuse an existing file identifier for another root. Keep the root type and
file identifier stable for the lifetime of a payload family. If a new contract
is intentionally unrelated to the old one, create a new root with a new file
identifier instead of silently repurposing the old identity.

## FlatBuffers Evolution Rules

Apply these rules to published tables, enums, unions, and structs:

- Add new table fields only at the end of the field list.
- Do not reorder existing fields.
- Do not change an existing field's scalar width, signedness, element type, or
  table type.
- Do not change a published default value.
- Do not remove a field slot. Deprecate it while preserving its position when
  removal from generated accessors is necessary.
- Treat field renames as generated API breaks even when the wire slot is
  unchanged.
- Append enum and union values; never renumber or reuse published values.
- Avoid `required` fields. Adding a required field is incompatible with older
  payloads and older writers.
- Treat any change to a FlatBuffers `struct` as breaking because structs have a
  fixed inline layout.
- Prefer tables for data expected to evolve.

Shared definitions in `common.fbs` affect every root that includes them. A
change there can change several generated payload IDs and public APIs, so review
and test it as a schema-set-wide change.

## Add a New Payload

1. Confirm that the result is persistent runtime data and belongs in the shared
   Perception contract rather than in model-local configuration or temporary
   operation state.
2. Reuse suitable tables from `common.fbs`. Add a shared definition only when
   multiple payload families have the same semantics.
3. Add a focused `.fbs` file under `schemas/perception/metadata/`. Use the
   existing payload files as templates.
4. Define the item tables, then define one transportable root table containing
   `schema_major`, `schema_minor`, optional `LayerInfo`, and the payload data.
5. Add one `root_type` and choose a descriptive, unique four-character
   `file_identifier`.
6. Record that a new payload normally requires a MINOR PEK release.
7. Hand off to `$regenerate-perception-sdk` and regenerate all configured SDK
   outputs:

   ```bash
   ./scripts/perception-sdk.sh generate
   ```

8. Review the generated C++, Python, and TypeScript API, payload IDs, schema-set digest, and
   manifest diff. Do not review only the `.fbs` file.
9. Add the required producer and consumer support. Common integration points are
   `development/ops-std/postproc/`, `development/elements/pekosd/`, tracker or
   performance elements, and Plumber inspection or comparison code.
10. Add tests for construction, serialization, deserialization, empty and
    optional fields, and the runtime behavior that consumes the payload.
11. Run the validation workflow below from a clean committed snapshot.

Example root shape:

```flatbuffers
include "common.fbs";

namespace perception.metadata;

table ExampleResult {
  object:perception.metadata.ObjectMeta;
  value:float;
}

table ExampleResults {
  schema_major:ushort = 1;
  schema_minor:ushort = 0;
  layer:perception.metadata.LayerInfo;
  results:[perception.metadata.ExampleResult];
}

root_type ExampleResults;
file_identifier "EXRS";
```

The identifier above is illustrative. Check the complete schema set before
selecting it for a real payload.

## Evolve an Existing Payload

Before editing a published schema:

1. Classify the proposed change as additive, API-breaking, wire-breaking, or a
   semantic change.
2. Run `python3 tools/perception/evaluate_schema_change.py --base <revision>`
   to establish the current compatibility baseline.
3. Identify all roots that include the changed file. Changes to `common.fbs`
   have wider impact than changes local to one root.
4. Decide whether consumers can upgrade together. The content-derived payload
   ID means mixed SDK versions do not provide typed access to both revisions
   automatically.
5. Prefer appending an optional table field for an additive change.
6. For an incompatible or meaning-changing redesign, consider a new root and
   file identifier so both contracts can coexist during migration.
7. Record the required PEK release impact according to the public API and semantics.
8. Regenerate and inspect all payload and manifest changes.
9. Add old/new fixture tests when mixed-version behavior matters. Test old
   payload bytes with the new schema and new payload bytes with the old schema,
   and state which direction is supported.

## Validation Workflow

Run the fastest schema-specific checks first:

```bash
./scripts/perception-sdk.sh check
python3 tools/perception/tests/test_release.py
PYTHONPATH="generated/perception/python/src:tools/plumber" \
  python3 -m unittest discover \
  -s tools/plumber/tests \
  -p 'test_frame_results_*.py'
```

For runtime-facing changes, build the elements with tests and run Meson tests:

```bash
./scripts/build.sh debug true
meson test -C /work/development/build --print-errorlogs
```

After the complete authored and generated snapshot is committed, hand off to
`$package-perception-sdk-release` to create and verify the release bundle:

```bash
./scripts/perception-sdk.sh package \
  --output-dir artifacts \
  --expect-version <MAJOR.MINOR.PATCH>

./scripts/perception-sdk.sh verify \
  artifacts/perception-sdk-<MAJOR.MINOR.PATCH>.zip \
  --require-sidecars
```

The package must contain the schemas, generated C++ SDK, generated Python wheel,
generated TypeScript npm package, matching FlatBuffers Python and TypeScript
runtimes, build integrations, release manifest, provenance, and checksum
sidecars. Use the project's pre-release package repository or release channel
for review candidates; do not weaken manifest or checksum validation.

## Review Checklist

- The change follows the FlatBuffers evolution rules above.
- The PEK release version matches the SDK API and semantic impact.
- Every root has one unique `file_identifier` and stable root identity.
- Shared-schema impact has been reviewed across all dependent roots.
- Generated C++, Python, and TypeScript outputs and manifests were regenerated, not edited.
- Runtime producers and consumers use the generated types.
- Compatibility expectations and migration behavior are covered by tests.
- `check`, release tests, affected builds, and affected runtime tests pass from
  a clean committed snapshot.
- The candidate bundle was packaged and verified before deployment.
