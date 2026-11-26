---
title: Build and use the Open Perception Kit bundle
sidebar_label: Open Perception Kit bundle
description: Build a reproducible Open Perception Kit C++, Python, Rust, and TypeScript archive and integrate it into an application.
---
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


# Build and use the Open Perception Kit bundle

The release bundle contains the generated C++ SDK, installable Python and
TypeScript packages, a Rust crate, matching FlatBuffers runtimes, the source
schemas, and a manifest describing every file and compatibility requirement.

Released OPK architecture packages carry the unchanged ZIP, checksum, and
provenance sidecar under `share/opk/open-perception-kit/`. SDK packages use
`version` from `tools/perception/sdk.json`, independently of the product version
in `development/meson.build`. OPK 0.1.4 includes SDK 1.0.0; use the SDK version
for the package names and dependencies below.

Open Perception Kit is licensed under Apache-2.0. Each language package includes
the licence text and Arm copyright notice, including the standalone wheel and
Cargo crate. Bundled third-party runtimes retain their own licences and notices.

This is the release-packaging workflow. During implementation, use
`$regenerate-perception-sdk` or `./scripts/perception-sdk.sh generate` to update
the tracked generated SDK snapshot and commit it normally. Use
`$package-open-perception-kit-release` only after that snapshot is committed.

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
and creates `artifacts/open-perception-kit-<sdk-version>.zip`.

After changing the SDK version in `tools/perception/sdk.json`, run
`./scripts/perception-sdk.sh generate`. A product-only version change does not
change the SDK identity or packages. The archive
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

Stable OPK releases publish the same verified open-perception-kit wheel that is embedded
in the SDK ZIP to the existing `edge-ai-tooling` Artifactory PyPI repository.
Python-only consumers can lock it as a normal package dependency.

Declare the SDK package version and named index:

```toml
[project]
dependencies = ["open_perception_kit==<sdk-version>"]

[[tool.uv.index]]
name = "edge-ai-tooling"
url = "https://artifactory.arm.com/artifactory/api/pypi/edge-ai-tooling.pypi/simple"
explicit = true
authenticate = "always"

[tool.uv.sources]
open_perception_kit = { index = "edge-ai-tooling" }
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

Use `open_perception_kit.packet` for serialized packets. The installed package includes
`guest.pyi` and `py.typed`, so editors can provide completion and type
information for guest scripts without importing the live bridge.

`open_perception_kit.guest` intentionally raises an import error in a normal Python
process. It becomes available only when a C++ host registers the generated
`open_perception_kit_bridge` module. A native OPK host registers it before starting
Python; a supported Python-hosted pipeline attaches it to the existing interpreter.

## Install the TypeScript SDK

Install both npm-compatible tarballs directly from the extracted bundle:

```bash
npm install \
  ./typescript/flatbuffers-25.9.23.tgz \
  ./typescript/open-perception-kit-<npm-package-version>.tgz
```

Use the npm package version from the extracted bundle's `README.md`, which
contains the exact install command. Development packages append `-dev.0` to the
SDK version when `package_prerelease` is enabled.

Import `Envelope`, `ProducerIdentityStatus`, and generated payload classes from
the `open-perception-kit` package. Require `Envelope.valid()` and an `exact_match`
or `compatible_match` producer identity before typed access. Both require the
expected SDK name; compatible versions share the same major. Schema digests are
diagnostic information, not an acceptance gate. SDK 1.0 producers and consumers
must be upgraded together from earlier SDKs. The package contains compiled ES modules,
TypeScript declarations, and generated sources; consumers do not regenerate it.

The embedded `opksink` WebUI bundles this same generated SDK with its authored
browser modules. Use `./scripts/opksink-web.sh check` to verify the committed
browser asset after SDK or WebUI changes.

## Integrate the Rust SDK

Stable OPK releases publish `open_perception_kit` at the SDK version on
[crates.io](https://crates.io/crates/open_perception_kit):

```bash
cargo add open_perception_kit
```

Pin the tested release in `Cargo.toml` and commit the consumer's `Cargo.lock`:

```toml
[dependencies]
open_perception_kit = "=<sdk-version>"
```

See the [crate README](https://crates.io/crates/open_perception_kit) for a runnable
example and the [Rust API reference](https://docs.rs/open_perception_kit) for types and selectors.

Snapshots are not published to crates.io. For snapshots or offline builds, use the bundle's
`rust/` crate, which pins its FlatBuffers runtime:

```toml
[dependencies]
open_perception_kit = { path = "/path/to/open-perception-kit-<sdk-version>/rust" }
```

The bundle includes checksum-locked Cargo archives, `rust/Cargo.lock`, and
`rust/vendor/`. Copy `rust/.cargo/config.toml` to the consumer's
`.cargo/config.toml`, setting `directory` to the absolute `rust/vendor` path.
Run from the consumer workspace:

```bash
cargo generate-lockfile --offline
cargo build --offline --locked
```

Cargo uses the consumer workspace's configuration and lockfile, not the path dependency's.

- Require successful `Envelope::decode` and `producer_identity()` of `ExactMatch`
  or `CompatibleMatch` before typed access: expected SDK name, valid version,
  and same SemVer major, including zero. Schema digests are diagnostic.
  SDK 1.0 requires coordinated producer and consumer upgrades from earlier SDKs;
  see the [compatibility contract](https://github.com/arm/open-perception-kit/blob/main/schemas/perception/README.md#compatibility-model).
- Select results by payload type and `LayerInfo.producer.instance_id`. Map
  geometry using `ImageSpace` and `CoordinateUnits`; coordinates are local to the
  described image region. See the
  [geometry schema](https://github.com/arm/open-perception-kit/blob/main/schemas/perception/metadata/common.fbs).
- For byte-exact forwarding, retain the original packet: repacking can lose
  unknown fields in recognized payloads, and `serialize()` stamps this SDK's
  producer identity.

## Integrate the C++ SDK

For CMake, include `cpp/cmake/open_perception_kit.cmake`, call
`open_perception_kit_enable_sdk()`, and link the application to `open_perception_kit::sdk`.

For Meson, vendor the complete `cpp/` directory into the project, call
`subdir('path/to/cpp/meson/open_perception_kit')`, and use `open_perception_kit_dep`. Bridge hosts
also call `subdir('path/to/cpp/meson/open_perception_kit/python_bridge')` and use
`open_perception_kit_python_bridge_dep`. The complete
tree is required because the integration references the generated headers and
optional Python bridge sources relative to its location.

Both integrations expose the required SDK, FlatBuffers, Python, and bridge
metadata. Install a compatible FlatBuffers C++ development package before
configuring the application.

## Verify provenance

`open-perception-kit-release-manifest.json` records the repository commit and dirty
state in the external provenance sidecar, while the reproducible archive records
descriptor and generation-manifest hashes, SDK identity and local generator
content hashes, exact FlatBuffers compiler and wheel, schema-set digest, generated payload
identities, locked build tools, and every bundle file hash. The descriptor and
source-generation manifest are retained under `metadata/`.

Verify an archive and its checksum/provenance sidecars with:

```bash
./scripts/perception-sdk.sh verify \
  artifacts/open-perception-kit-<sdk-version>.zip \
  --require-sidecars
```

The artifact directory is a checksum-verified read-write cache: missing locked
artifacts are downloaded atomically and reused by later builds.

Use the `check`, `package`, and `verify` commands directly when validating a
release locally. Release CI builds the triplet once from the selected immutable
commit, embeds the same bytes in both architecture packages, and verifies it
again after each package is extracted for smoke testing.
