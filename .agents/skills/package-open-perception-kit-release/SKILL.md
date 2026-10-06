---
name: package-open-perception-kit-release
description: Build and verify the deterministic Open Perception Kit release ZIP, checksum, and provenance sidecar from an already committed canonical generated snapshot. Use for release candidates, pre-release deployment artifacts, reproducibility checks, offline bundle creation, verification of an existing open-perception-kit bundle, or handoff into OPK product package assembly. Do not use this skill to modify schemas or regenerate checked-in SDK sources.
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


# Package an Open Perception Kit release

Create a release bundle from immutable committed SDK inputs. This workflow
produces distributable artifacts and must not modify generated source files.

## Confirm the Workflow

Use this workflow only after implementation is complete and the authored and
generated SDK snapshot has been committed normally. If schemas or generated
sources still need changes, stop and use `$evolve-perception-schema` or
`$regenerate-perception-sdk` first.

The release bundle contains the C++ SDK, open-perception-kit Python wheel, Rust crate,
and TypeScript npm package, matching FlatBuffers runtimes including a
checksum-locked Cargo vendor directory, source schemas, build integrations,
release manifest, ZIP checksum, and provenance sidecar.

## Preflight the Snapshot

1. Read `tools/perception/sdk.json` and
   `docs/public/how-to/use-perception-sdk.md`.
2. Confirm the intended release commit and SDK version.
3. Require a clean tracked tree:

   ```bash
   git diff --quiet
   git diff --cached --quiet
   git rev-parse HEAD
   ```

4. Confirm that the checked-in generated snapshot is reproducible:

   ```bash
   ./scripts/perception-sdk.sh check
   python3 tools/perception/tests/test_release.py
   ```

The packaging command verifies the checked-in generation receipt but never
invokes the generator, `flatc`, or formatters. Do not use `--allow-dirty` for a
release candidate. It is reserved for local experiments and records
`dirty=true` in provenance.

## Build the Bundle

Read the stable OPK version from `development/meson.build`, then assert the same
value during packaging:

```bash
sdk_version="$(sed -n "s/^[[:space:]]*version: '\([^']*\)'.*/\1/p" development/meson.build)"

./scripts/perception-sdk.sh package \
  --output-dir artifacts \
  --expect-version "${sdk_version}"
```

For an offline or cached build, use the checksum-locked artifact cache. It must
contain the locked Python, TypeScript, and Rust Cargo artifacts from
`tools/perception/sdk.json`:

```bash
./scripts/perception-sdk.sh package \
  --output-dir artifacts \
  --expect-version "${sdk_version}" \
  --artifact-dir /path/to/sdk-cache
```

Use `--flatbuffers-wheel FILE` only when supplying the exact filename and
SHA-256 locked by `tools/perception/sdk.json`. Use `--python EXECUTABLE` when a
specific interpreter must create the isolated wheel-build environment.

## Verify the Release

Require both release sidecars:

```bash
./scripts/perception-sdk.sh verify \
  "artifacts/open-perception-kit-${sdk_version}.zip" \
  --require-sidecars
```

Confirm that these files exist:

- `open-perception-kit-${sdk_version}.zip`
- `open-perception-kit-${sdk_version}.zip.sha256`
- `open-perception-kit-${sdk_version}.zip.provenance.json`

Confirm that provenance records the intended repository commit and
`dirty=false`. The archive verifier checks deterministic ZIP metadata, safe
paths, bundle manifests, file hashes, wheel metadata, schema identity, and
sidecar integrity.

For explicit reproducibility qualification, build twice from the same commit
and locked artifact cache into separate output directories, then compare the
two ZIP SHA-256 values.

## Publish or Hand Off

Choose one handoff mode:

- **Standalone open-perception-kit handoff:** When open-perception-kit itself is the
  requested deliverable, upload the open-perception-kit ZIP file with the SHA and
  provenance to the chosen artifactory location.
- **OPK product release handoff:** Pass the verified triplet only to the existing
  OPK package assembly. Require the same bytes under
  `share/opk/open-perception-kit/` in both architecture archives. Stable release
  pushes publish the verified Python wheel unchanged to the existing
  Artifactory PyPI repository. The Arm release build packages and verifies
  the bundle's prepared Rust tree using its locked vendor directory. The
  release-only manifest must record FlatBuffers as a crates.io dependency. The
  publication job must reject an existing version before generic Artifactory
  publication, publish the staged source through Cargo's native protocol using
  the existing anonymous Cargo principal on the explicit eu02 route, then
  verify that the registered crate matches the Arm build's package and
  sparse-index checksum. A
  clean, exact-pinned Cargo 1.85 consumer must build it on x86_64 and ARM64
  without FlatBuffers generation. A red release
  requires release-owner cleanup before retry. Public distribution must use
  authenticated, server-enforced immutable publication instead.
  Generic Artifactory keeps the OPK archives. Manual snapshots instead
  place the wheel and crate beside those archives in their immutable generic
  Artifactory folder.
  Do not publish the rest of the triplet as separate top-level OPK release
  assets.

Do not commit release ZIPs or sidecars unless repository policy explicitly
requires it. Record the SDK version, repository commit, archive SHA-256,
handoff mode, and destination in the release task.

## Verify an Existing Artifact

To validate a received or downloaded bundle without rebuilding it:

```bash
./scripts/perception-sdk.sh verify \
  /path/to/open-perception-kit-<MAJOR.MINOR.PATCH>.zip \
  --require-sidecars
```

Verification requires adjacent checksum and provenance files when
`--require-sidecars` is used.

## Report

State the SDK version, source commit, clean-tree status, output paths, archive
SHA-256, verification result, artifact-cache mode, handoff mode, and
publication destination. For an OPK product release, identify both enclosing
architecture archives. Do not call the bundle release-ready if source drift
exists, provenance is dirty, sidecars are missing, or verification fails.
