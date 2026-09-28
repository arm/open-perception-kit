<!-- SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates -->

# flowdata SDK Generator

`gen.py` generates project-named C++, Python, Rust, and TypeScript SDKs from one
FlatBuffers schema set. The generator remains project agnostic: consuming
projects own release decoration, archive layout, signing, and publication.

## Commands

Show the generator identity:

```bash
python3 tools/flowdata/gen.py --version
# sdkgen 0.6.1
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

- `--name` is the schema namespace and the default public SDK name. It
  must match `[a-z][a-z0-9_]*` and cannot be a target-language keyword.
- `--public-name` sets the package, namespace, and build-variable prefix for
  every generated language. It defaults to `--name`.
- `--version` is the generated SDK release version and must be stable semantic
  versioning in `MAJOR.MINOR.PATCH` form.
- `--schema-dir` is the complete schema-set root. Every transportable payload
  has one `root_type` and one four-character `file_identifier`.
- `--generated-root` defaults to `generated`.
- `--flatc` defaults to `flatc` on `PATH`.

The supported FlatBuffers compiler range is `>=24.3.25,<26.0.0`. Python and
TypeScript package metadata require the same runtime range. Generated C++
headers, build integrations, and Rust crates require the exact FlatBuffers
version used by `flatc`.

## Output Layout

Each invocation stages and verifies new output, then replaces the complete
selected language root. This prevents files from a previous feature set or schema
set surviving regeneration.

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

The manifest does not hash itself. Generated-file and package/bridge descriptor
paths are normalized POSIX paths relative to the manifest's SDK root. Schema
paths are relative to the input schema directory. Absolute paths and external
logical roots are not supported.

`verify-manifest` rejects malformed metadata, duplicate JSON keys, missing or
extra SDK files, unsafe paths, symlink escapes, size or hash mismatches, invalid
payload identities, incompatible FlatBuffers requirements, and inconsistent
feature descriptors. With `--schema-root`, it also verifies the complete schema
file set and aggregate digest.

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

For reproducible releases, the project records local generator source hashes,
the SDK semantic version, the exact `flatc` executable, and the schema set. Rust
generation also needs a pinned `rustfmt` version and configuration. The project
generates once, verifies the manifests, then packages those verified files
without regeneration. Project-specific headers or archive metadata are applied
and hashed by the consuming project's release process.
