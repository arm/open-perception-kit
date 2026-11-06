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

# Generator Architecture

The generator is a small, stdlib-only Python application around `flatc`.
Its source has three distinct kinds of code: generation/orchestration logic,
schema-dependent emitters, and handwritten target-language runtime templates.
Generated SDK files remain outputs, never the source of truth.

## Generation flow

```text
gen.py → app.py
          ├─ validate CLI inputs, compiler version and output options
          ├─ discover schemas → validate namespaces → assign IDs → validate entries
          ├─ create a staging directory
          │    ├─ selected language generator
          │    ├─ requested C++ build integrations
          │    └─ write and verify the generation manifest
          └─ install staged SDK with existing backup/rollback behavior
```

The CLI and staging transaction stay in `engine/app.py`. Language backends
receive a `GenerationContext` and payload entries and return generated paths.
The small static registries select existing backends; there is no plugin system.

## Ownership map

Paths below are relative to `engine/`.

| Area | Source of truth |
|---|---|
| CLI, context construction, installation | `app.py` |
| Shared input models | `types.py` |
| Generator release identity (separate from generated SDK versions) | `version.py` |
| Root discovery and validation | `discovery.py`, `validation.py` |
| Payload IDs and schema-set hashing | `schema_set.py` |
| Envelope wire schema, shared by all targets | `wire_schema.py` |
| Compiler/runtime support policy | `flatbuffers_compat.py`, `python_compat.py` |
| C++ compiler invocation and header rendering | `cpp/cpp_sdk_codegen.py` |
| C++ header runtime literal | `cpp/header_template.py` |
| Python generation, imports, registry and packaging | `python/python_sdk_codegen.py` |
| Python packet runtime and public facade/stub literals | `python/runtime_template.py`, `python/api_templates.py` |
| TypeScript generation, naming, registry and packaging | `typescript/ts_sdk_codegen.py` |
| TypeScript envelope runtime literal | `typescript/runtime_template.py` |
| Rust crate generation and runtime | `rust/rust_sdk_codegen.py` |
| CMake and Meson output | `cpp/cmake.py`, `cpp/meson.py` |
| Manifest API, descriptors, primitives and writer | `manifest.py` |
| Manifest output/runtime and payload consistency | `manifest_contract.py` |
| Manifest schema/output file verification | `manifest_files.py` |

Manifest verification order is intentional: header metadata, schema records,
output/runtime contracts, payload records, then generated-file integrity.
Helpers retain that order and use the public module's validators/descriptors.
Changing which invalid condition wins is a behavior change.

## Version ownership

flowdata-sdk owns version syntax validation, propagation into artifacts/envelopes,
and advisory identity comparison. The consuming project that owns the schemas
chooses the generated SDK version and the semantic reasons for major/minor/patch
bumps; the generator does not infer or enforce those decisions.

All runtime implementations check name first, then stable version syntax, then
full-version equality, then major-component equality. The same-major rule includes
zero. The generator's own development/release version never participates in packet
compatibility. Keep this contract synchronized with `AGENTS.md` and the SDK guides.

## C++ Python bridge

`cpp/python_bridge.py` remains the stable generation entrypoint and the defining
module for `BridgeTypeRef`, `BridgeField`, and `BridgeType`. Existing helpers
remain callable there because consuming projects use them directly.

The implementation is organized by responsibility:

| Module under `cpp/` | Responsibility |
|---|---|
| `bridge_schema.py` | Schema scanning, declaration parsing and type resolution |
| `bridge_values.py` | Native type spelling, field access and value-conversion fragments |
| `bridge_proxies.py` | Proxy declarations, getters, vectors and type registration |
| `bridge_dispatch.py` | Known-payload dispatch and its interpolated CPython runtime |
| `bridge_runtime_template.py` | Bridge header, proxy storage and live-envelope support |
| `bridge_module_template.py` | CPython registration and host wrapper lifetime |

The entrypoint imports implementation modules at call time. Those modules can
therefore reference the original models without partially initialized imports.
Collaborator calls resolve through `python_bridge`, preserving existing patch
points—including constructors patched before or after the first parser call.
The explicit forwarding functions are compatibility boundaries, not a new
extension mechanism. Do not replace them with copied globals or generated
delegation machinery.

The runtime fragments still assemble into the same single generated bridge
source and header. Keep their declaration order and interpolation semantics.

## Template conventions

- Templates live in ordinary Python modules. Consumers such as OPK inventory
  Python source files; moving templates into another resource format would need
  a separate distribution/provenance change.
- Rendering stays at the original backend boundary. Preserve banners, escapes,
  indentation, final newlines, substitution order and whole-string transforms.
- The C++ header template intentionally contains doubled braces, unescaped by
  its renderer after schema-specific insertion.
- Python and TypeScript runtime literals are dedented and substituted only by
  their renderers. Do not independently strip or dedent fragments.
- Bridge dispatch uses an f-string. Keep evaluation order; replacing it with
  repeated token substitution is not a mechanical extraction.
- Small schema-specific fragments belong beside their emitters. Do not create
  a file for every getter, declaration, or mapping.

File size is a guide, not a quota. The complete C++ envelope template and the
bridge dispatch f-string are intentionally longer cohesive units. Splitting
their ownership/locking or interpolation invariants would add navigation and
preservation risk without helping the generator's orchestration code.

## Structural-change validation

In a full repository checkout, use the existing regression suite and
`tests/generator_equivalence.py`.
The differential harness runs baseline and candidate in isolated processes with
the same interpreter/compiler, schemas and output paths. It checks generated
bytes, manifests, CLI diagnostics, APIs, renderer edge cases, compiler commands,
patch behavior, and staged-installation failures. See
`tests/README.md` for commands. Test sources are not shipped in the generator
source package.

Source files and their fingerprints naturally change in a refactor. Direct SDK
outputs must not. A consuming project's source-provenance receipt must be
regenerated when it adopts changed generator sources; that is separate from the
raw SDK manifest. Keep runtime fixes, parser changes, validation changes, version
bumps and formatting of generated output in separately reviewed changes.
