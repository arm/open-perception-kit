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

# flowdata SDK Generator

`gen.py` generates project-named C++, Python, Rust, and TypeScript SDKs from one
FlatBuffers schema set. The generator remains project agnostic: consuming
projects own release decoration, archive layout, signing, and publication.

For generator maintenance, see [ARCHITECTURE.md](ARCHITECTURE.md): source
ownership, template conventions, compatibility boundaries and refactor checks.

## Commands

Commands below are run from the repository root. `schemas` is an example
consumer-owned schema directory; use `payloads` for the repository demo schemas.

Show the generator identity:

```bash
python3 tools/flowdata/gen.py --version
# sdkgen 0.7.1
```

Generate one language SDK:

```bash
python3 tools/flowdata/gen.py generate \
  --name project_sdk \
  --version 1.2.3 \
  --sdk cpp \
  --schema-dir schemas \
  --generated-root generated \
  --flatc "$(which flatc)" \
  --cmake \
  --meson \
  --cpp-python-bridge
```

Use `--sdk python`, `--sdk rust`, or `--sdk ts` for the other languages. CMake, Meson, and the
embedded-Python bridge are C++-only outputs.
Rust generation also requires `rustfmt` on `PATH`.

Verify generated output:

```bash
python3 tools/flowdata/gen.py verify-manifest \
  generated/cpp/flowdata-manifest.json \
  --schema-root schemas
```

## Inputs

- `--name` is the generated package, namespace, and build-variable prefix. It
  must match `[a-z][a-z0-9_]*` and cannot be a target-language keyword.
  Python standard-library names and runtime dependencies (`flatbuffers`, `numpy`)
  are also rejected to prevent import collisions.
- `--version` is the generated SDK release version and must be stable semantic
  version syntax in `MAJOR.MINOR.PATCH` form. The publishing project assigns it;
  the generator does not verify release-to-release compatibility or version bumps.
- `--schema-dir` is the complete schema-set root. Every transportable payload
  has one `root_type` and one four-character `file_identifier`. Discovery is
  recursive; `.fbs` basenames must be unique across the directory. Support
  schemas have no `root_type`.
- `--sdk` selects `cpp`, `python`, `rust`, or `ts` and defaults to `cpp`.
- `--generated-root` defaults to `generated`.
- `--flatc` defaults to `flatc` on `PATH`.

The supported FlatBuffers compiler range is `>=24.3.25,<26.0.0`. Python and
TypeScript package metadata require the same runtime range. Generated C++
headers, build integrations, and Rust crates require the exact FlatBuffers
version used by `flatc`.

## Payload Identity And Compatibility

The generator's own version supports `MAJOR.MINOR.PATCH` and
`MAJOR.MINOR.PATCH-dev`. Development versions are recorded in generated manifests
and untagged source packages; tagged releases require a stable version. This is
separate from the generated SDK's stable-only `--version` argument. See the
[release guide](../../docs/generator-release.md#development-and-release-versions).

Known payload ids hash only the case-sensitive, canonical dotted FlatBuffers
root name encoded as UTF-8. Calculate SHA-256, interpret its first eight bytes
as an unsigned big-endian integer, then compute `value % (2**63 - 1) + 1`.
There is no salt or SDK version input. File identifiers, paths, schema contents,
and include contents do not affect ids. Duplicate generated ids are rejected.
Schema hashes remain independent manifest/provenance data.
Root names follow `flatc`'s root-specific lookup: try the exact name first, then
prefix it with the active namespace. Parent namespaces are not searched. Lookup
uses declarations available at the `root_type` statement, including transitive
includes; later declarations cannot change that selection. Relative and fully
qualified spellings that resolve to the same root produce the same id.

The SDK's producer-identity check is advisory: same name and full version gives
`exact_match`; different versions sharing a major give
`compatible_match`. Other names give `name_mismatch`; invalid or incompatible
versions give `version_mismatch`. The same-major rule includes major zero.
The schema digest does not affect the result. Publishers must honor compatible
wire evolution in both directions within a major; applications choose acceptance
policy. Matching ids or successful decoding alone do not prove compatibility.

Generator 0.7.0 intentionally replaces existing ids and the old
six-state identity enum. Regenerate and coordinate deployment of producer/reader
SDKs; there is no legacy-id aliasing. See the
[integration guide](../../docs/integration-guide.md#compatibility-model).

## Output Layout

Each invocation generates and verifies a staged SDK, then replaces the complete
selected language root using renames with backup/rollback handling. Include all
needed integration/bridge flags on each invocation; previous features, build
products and local additions in that language root are not retained. Other
language roots are unaffected.
If installation and restoration both fail, generation reports a retained recovery
directory containing the previous SDK under `previous/`; recover it before
removing that directory.

```text
generated/
├── cpp/
│   ├── project_sdk.h
│   ├── fb/
│   ├── cmake/project_sdk.cmake
│   ├── meson/project_sdk/meson.build
│   ├── python_bridge/                 # optional
│   └── flowdata-manifest.json
├── python/
│   ├── pyproject.toml
│   ├── src/project_sdk/
│   │   ├── packet.py
│   │   ├── guest.py
│   │   ├── guest.pyi
│   │   └── py.typed
│   └── flowdata-manifest.json
├── rust/
│   ├── Cargo.toml
│   ├── src/
│   └── flowdata-manifest.json
└── ts/
    ├── package.json
    ├── src/
    └── flowdata-manifest.json
```

The Python `packet` API owns serialized packet data. The Python `guest` API is
available only inside a registered generated C++ bridge host and borrows a live
C++ envelope for the duration of a host callback.

## Generation Manifest

Every language root contains `flowdata-manifest.json`. There is one current,
strict manifest shape; it has no independent format version. The generator and
verifier are released together, and unknown or missing fields are rejected.

The manifest records:

- SDK name and semantic version
- generator name and version
- exact `flatc` identity and supported compiler requirement
- required FlatBuffers runtimes
- selected language, integrations, and bridge feature
- complete schema file records and aggregate schema-set SHA-256
- generated payload identities
- Python package or bridge descriptors when applicable
- every generated file path, size, and SHA-256

The manifest does not hash itself. Generated-file and package/bridge artifact
paths are relative to the manifest's SDK root. Schema-file and payload `schema`
paths are relative to the input schema directory. Paths use relative POSIX
notation; absolute paths and extra logical output roots are not supported.

`verify-manifest` rejects malformed metadata, duplicate JSON keys, missing or
extra SDK files, unsafe paths, directory/broken symlinks, escaping file symlinks,
size or hash mismatches, invalid payload identities, incompatible FlatBuffers
requirements, and inconsistent feature descriptors. With `--schema-root`, it also verifies the complete schema
file set and aggregate digest.

Verify pristine output before building, importing, installing or decorating it.
For example, `dist/`, `node_modules/`, Python bytecode caches and wheel-build
metadata are additional files that cause complete-root verification to fail.
Keep a pristine copy for later verification, or regenerate before verifying.

## C++ Integration

CMake consumers include `generated/cpp/cmake/project_sdk.cmake`, call
`project_sdk_enable_sdk()`, and link `project_sdk::sdk`. Bridge hosts call
`project_sdk_enable_python_bridge(target)` when the bridge was generated.

Meson consumers call `subdir('generated/cpp/meson/project_sdk')` and use
`project_sdk_dep`. When the bridge was generated, also call
`subdir('generated/cpp/meson/project_sdk/python_bridge')` to expose
`project_sdk_python_bridge_dep`.

Both integrations expose the SDK version, exact FlatBuffers requirement, Python
requirement, and bridge availability using identifiers derived from `--name`.

## Reproducibility

For reproducible releases, the consuming project pins the flowdata-sdk commit,
the SDK semantic version, the exact `flatc` executable, and the schema set. Rust
generation also needs a pinned `rustfmt` version and configuration. The project
generates once, verifies the manifests, then packages those verified files
without regeneration. Project-specific headers or archive metadata are applied
and hashed by the consuming project's release process.
