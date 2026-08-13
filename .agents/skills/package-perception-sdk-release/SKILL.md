---
name: package-perception-sdk-release
description: Build and verify the deterministic Perception SDK release ZIP, checksum, and provenance sidecar from an already committed canonical generated snapshot. Use for release candidates, pre-release deployment artifacts, reproducibility checks, offline bundle creation, or verification of an existing Perception SDK bundle. Do not use this skill to modify schemas or regenerate checked-in SDK sources.
---

# Package Perception SDK Release

Create a release bundle from immutable committed SDK inputs. This workflow
produces distributable artifacts and must not modify generated source files.

## Confirm the Workflow

Use this workflow only after implementation is complete and the authored and
generated SDK snapshot has been committed normally. If schemas or generated
sources still need changes, stop and use `$evolve-perception-schema` or
`$regenerate-perception-sdk` first.

The release bundle contains the C++ SDK, Perception Python wheel and TypeScript
npm package, matching FlatBuffers runtimes, source schemas, build integrations,
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

Read the stable version from `tools/perception/sdk.json`, then assert the same
value during packaging:

```bash
sdk_version="$(python3 -c \
  'import json; print(json.load(open("tools/perception/sdk.json"))["version"])')"

./scripts/perception-sdk.sh package \
  --output-dir artifacts \
  --expect-version "${sdk_version}"
```

For an offline or cached build, use the checksum-locked artifact cache:

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
  "artifacts/perception-sdk-${sdk_version}.zip" \
  --require-sidecars
```

Confirm that these three files exist:

- `perception-sdk-${sdk_version}.zip`
- `perception-sdk-${sdk_version}.zip.sha256`
- `perception-sdk-${sdk_version}.zip.provenance.json`

Confirm that provenance records the intended repository commit and
`dirty=false`. The archive verifier checks deterministic ZIP metadata, safe
paths, bundle manifests, file hashes, wheel metadata, schema identity, and
sidecar integrity.

For explicit reproducibility qualification, build twice from the same commit
and locked artifact cache into separate output directories, then compare the
two ZIP SHA-256 values.

## Publish or Hand Off

Upload all three files together to the chosen pre-release or release channel.
Do not commit release ZIPs or sidecars unless repository policy explicitly
requires it. Record the SDK version, repository commit, archive SHA-256, and
destination in the release task.

## Verify an Existing Artifact

To validate a received or downloaded bundle without rebuilding it:

```bash
./scripts/perception-sdk.sh verify \
  /path/to/perception-sdk-<MAJOR.MINOR.PATCH>.zip \
  --require-sidecars
```

Verification requires adjacent checksum and provenance files when
`--require-sidecars` is used.

## Report

State the SDK version, source commit, clean-tree status, output paths, archive
SHA-256, verification result, artifact-cache mode, and publication destination.
Do not call the bundle release-ready if source drift exists, provenance is
dirty, sidecars are missing, or verification fails.
