---
title: Build and use the Perception SDK bundle
sidebar_label: Perception SDK bundle
description: Build a reproducible Perception C++, Python, Rust, and TypeScript SDK archive and integrate it into an application.
---

# Build and use the Perception SDK bundle

The release bundle contains the generated C++ SDK, installable Python and
TypeScript packages, a Rust crate, matching FlatBuffers runtimes, the source
schemas, and a manifest describing every file and compatibility requirement.

Released OPK architecture packages carry the unchanged ZIP, checksum, and
provenance sidecar under `share/opk/perception-sdk/`. SDK packages use the OPK
product version from `development/meson.build`.

The Perception SDK is licensed under Apache-2.0. Each language package includes
the licence text and Arm copyright notice, including the standalone wheel and
Cargo crate. Bundled third-party runtimes retain their own licences and notices.

This is the release-packaging workflow. During implementation, use
`$regenerate-perception-sdk` or `./scripts/perception-sdk.sh generate` to update
the tracked generated SDK snapshot and commit it normally. Use
`$package-perception-sdk-release` only after that snapshot is committed.

## Build the bundle

Start the OPK development container, then run from the repository root:

```bash
./scripts/perception-sdk.sh package --output-dir artifacts
```

The unified command surface is `./scripts/perception-sdk.sh`; it provides
`generate`, `check`, `package`, `verify`, and `install-dev` subcommands. The
script is the only supported SDK command surface.

The command verifies `tools/perception/sdk.json`, the checked-in generated SDK,
internal Meson adapter, generation receipt, and local generator content hashes
without invoking flowdata-sdk, `flatc`, or formatters. The generator sources are
normal tracked files in `tools/flowdata-sdk`, updated manually alongside the
regenerated snapshot; SDK commands do not fetch or update them.
It builds the Python wheels from the canonical snapshot
and creates `artifacts/perception-sdk-<opk-version>.zip`.

After changing the OPK product version in `development/meson.build`, run
`./scripts/perception-sdk.sh generate`. The archive
uses fixed timestamps, permissions, ordering, and compression so identical
inputs produce identical bytes. Packaging rejects dirty SDK inputs and outputs
unless `--allow-dirty` is explicitly supplied. CI can assert a release version
with `--expect-version MAJOR.MINOR.PATCH`.

Repository paths are also defined only in `tools/perception/sdk.json`; all SDK
tools consume them through `tools/perception/sdk_config.py`.

For an offline build, place the locked FlatBuffers runtime packages and Python
build-tool wheels in one directory:

```bash
./scripts/perception-sdk.sh package \
  --artifact-dir /path/to/locked-wheels
```

Every filename and SHA-256 must match `tools/perception/sdk.json`. The same
descriptor also locks the C++ source archive used by the Docker images.

## Install the Python SDK

Stable OPK releases publish the same verified Perception wheel that is embedded
in the SDK ZIP to the existing `edge-ai-tooling` Artifactory PyPI repository.
Python-only consumers can lock it as a normal package dependency.

Declare the OPK release version and named index:

```toml
[project]
dependencies = ["opk-perception-sdk==<opk-version>"]

[[tool.uv.index]]
name = "edge-ai-tooling"
url = "https://artifactory.arm.com/artifactory/api/pypi/edge-ai-tooling.pypi/simple"
explicit = true
authenticate = "always"

[tool.uv.sources]
opk-perception-sdk = { index = "edge-ai-tooling" }
```

Manual integration snapshots are not published as stable PyPI versions. Their
wheel remains available at the exact generic Artifactory snapshot URL printed
by the release job and can be temporarily pinned with its SHA-256.

Authenticate uv with the existing Artifactory credentials, then lock and sync:

```bash
printf '%s' "${ARTIFACTORY_TOKEN:?required}" | uv auth login artifactory.arm.com \
  --username "${ARTIFACTORY_USERNAME:?required}" \
  --password -
uv lock
uv sync --locked
```

For an offline bundle installation, extract the archive and run:

```bash
python3 -m pip install \
  --no-index \
  --find-links python \
  -r python/requirements.txt
```

Use `perception.packet` for serialized packets. The installed package includes
`guest.pyi` and `py.typed`, so editors can provide completion and type
information for guest scripts without importing the live bridge.

`perception.guest` intentionally raises an import error in a normal Python
process. It becomes available only when a C++ host registers the generated
`perception_bridge` module before starting Python.

## Install the TypeScript SDK

Install both npm-compatible tarballs directly from the extracted bundle:

```bash
npm install \
  ./typescript/flatbuffers-25.9.23.tgz \
  ./typescript/perception-<opk-version>.tgz
```

Import `Envelope`, `ProducerIdentityStatus`, and generated payload classes from
the `perception` package. Require `Envelope.valid()` and an `exact_match`
producer identity before typed access. The package contains compiled ES modules,
TypeScript declarations, and generated sources; consumers do not regenerate it.

The embedded `opksink` WebUI bundles this same generated SDK with its authored
browser modules. Use `./scripts/opksink-web.sh check` to verify the committed
browser asset after SDK or WebUI changes.

## Integrate the Rust SDK

Stable OPK releases publish the `perception` crate at the OPK version to the
`edge-ai-tooling` Cargo registry. Configure its sparse index:

```toml
[registries.edge-ai-tooling]
index = "sparse+https://artifactory.arm.com/artifactory/api/cargo/edge-ai-tooling.cargo/index/"
```

Select that registry only for Perception. The published crate metadata assigns
its FlatBuffers dependency to crates.io explicitly:

```toml
[dependencies]
perception = { version = "=<opk-version>", registry = "edge-ai-tooling" }
```

Manual snapshots do not publish to the Cargo registry. For a snapshot or an
offline build, add the extracted `rust/` crate as a path dependency. The crate
already pins the FlatBuffers runtime version used to generate its sources. The
bundle also contains checksum-locked Cargo archives, a generated `Cargo.lock`,
and a `rust/vendor/` directory:

```toml
[dependencies]
perception = { path = "/path/to/perception-sdk-<opk-version>/rust" }
```

Copy `rust/.cargo/config.toml` into the consumer's `.cargo/config.toml` and
change its `directory` value to the absolute path of the extracted `rust/vendor`
directory. Generate the consumer lockfile, then build without accessing the
registry:

```bash
cargo generate-lockfile --offline
cargo build --offline --locked
```

Cargo configuration and the consumer lockfile are resolved from the consumer
workspace, not from path dependencies.

Import `Envelope`, `payload`, and generated native payload types from
`perception`. Construct an envelope with `Envelope::decode(...)`, require a
successful result, and check `producer_identity()` before typed access. Use the
same selector for `count`, `contains`, `get`, and `for_each`; use `external_key`
for external
payloads. Unknown or changed payloads remain preserved across serialization.

## Integrate the C++ SDK

For CMake, include `cpp/cmake/perception.cmake`, call
`perception_enable_sdk()`, and link the application to `perception::sdk`.

For Meson, vendor the complete `cpp/` directory into the project, call
`subdir('path/to/cpp/meson/perception')`, and use `perception_dep`. Bridge hosts
also call `subdir('path/to/cpp/meson/perception/python_bridge')` and use
`perception_python_bridge_dep`. The complete
tree is required because the integration references the generated headers and
optional Python bridge sources relative to its location.

Both integrations expose the required SDK, FlatBuffers, Python, and bridge
metadata. Install a compatible FlatBuffers C++ development package before
configuring the application.

## Verify provenance

`perception-sdk-release-manifest.json` records the repository commit and dirty
state in the external provenance sidecar, while the reproducible archive records
descriptor and generation-manifest hashes, SDK identity and local generator
content hashes, exact FlatBuffers compiler and wheel, schema-set digest, generated payload
identities, locked build tools, and every bundle file hash. The descriptor and
source-generation manifest are retained under `metadata/`.

Verify an archive and its checksum/provenance sidecars with:

```bash
./scripts/perception-sdk.sh verify \
  artifacts/perception-sdk-<opk-version>.zip \
  --require-sidecars
```

The artifact directory is a checksum-verified read-write cache: missing locked
artifacts are downloaded atomically and reused by later builds.

Use the `check`, `package`, and `verify` commands directly when validating a
release locally. Release CI builds the triplet once from the selected immutable
commit, embeds the same bytes in both architecture packages, and verifies it
again after each package is extracted for smoke testing.
