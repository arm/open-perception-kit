---
title: Build and use the Perception SDK bundle
sidebar_label: Perception SDK bundle
description: Build a reproducible Perception C++ and Python SDK archive and integrate it into an application.
---

# Build and use the Perception SDK bundle

The release bundle contains the generated C++ SDK, an installable Perception
Python wheel, the matching FlatBuffers Python wheel, the source schemas, and a
manifest describing every file and compatibility requirement.

This is the release-packaging workflow. During implementation, use
`$regenerate-perception-sdk` or `./scripts/perception-sdk.sh generate` to update
the tracked generated SDK snapshot and commit it normally. Use
`$package-perception-sdk-release` only after that snapshot is committed.

## Build the bundle

Start the PEK development container, then run from the repository root:

```bash
./scripts/perception-sdk.sh package --output-dir artifacts
```

The unified command surface is `./scripts/perception-sdk.sh`; it provides
`generate`, `check`, `package`, `verify`, and `install-dev` subcommands. The
script is the only supported SDK command surface.

The command verifies `tools/perception/sdk.json`, the checked-in generated SDK,
internal Meson adapter, and generation receipt without invoking flowdata-sdk,
`flatc`, or formatters. It builds the Python wheels from the canonical snapshot
and creates `artifacts/perception-sdk-<sdk-version>.zip`.

Change the stable `MAJOR.MINOR.PATCH` version only in
`tools/perception/sdk.json`, then run `./scripts/perception-sdk.sh generate`. The archive
uses fixed timestamps, permissions, ordering, and compression so identical
inputs produce identical bytes. Packaging rejects dirty SDK inputs and outputs
unless `--allow-dirty` is explicitly supplied. CI can assert a release version
with `--expect-version MAJOR.MINOR.PATCH`.

Repository paths are also defined only in `tools/perception/sdk.json`; all SDK
tools consume them through `tools/perception/sdk_config.py`.

For an offline build, place the locked FlatBuffers and Python build-tool wheels
in one directory:

```bash
./scripts/perception-sdk.sh package \
  --artifact-dir /path/to/locked-wheels
```

Every filename and SHA-256 must match `tools/perception/sdk.json`. The same
descriptor also locks the C++ source archive used by the Docker images.

## Install the Python SDK

Extract the archive and run:

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
descriptor and generation-manifest hashes, SDK and flowdata-sdk identities,
exact FlatBuffers compiler and wheel, schema-set digest, generated payload
identities, locked build tools, and every bundle file hash. The descriptor and
source-generation manifest are retained under `metadata/`.

Verify an archive and its checksum/provenance sidecars with:

```bash
./scripts/perception-sdk.sh verify \
  artifacts/perception-sdk-<sdk-version>.zip \
  --require-sidecars
```

The artifact directory is a checksum-verified read-write cache: missing locked
artifacts are downloaded atomically and reused by later builds.

Use the `check`, `package`, and `verify` commands directly when validating a
release locally. CI integration can be added later when the release workflow is
ready to become a required project gate.
