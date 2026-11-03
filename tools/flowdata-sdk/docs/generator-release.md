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

# Generator Source Releases

The generator source package is distinct from a generated application SDK.
It contains the Python generator and consumer documentation, not generated SDKs,
demo schemas, demos, tests, workflows, compiler binaries, or runtime dependencies.
Consumers supply their own schemas and retain ownership of generated SDK releases.

## Package Contract

The release assets are:

```text
flowdata-sdk-generator-<version>.tar.gz
flowdata-sdk-generator-<version>.tar.gz.sha256
```

The archive has one `flowdata-sdk-generator-<version>/` directory. It preserves
the repository-relative `tools/flowdata/` layout, includes the root overview,
agent, concept, and limitations documents plus the consumer guides under `docs/`,
and includes `LICENSE` and `NOTICE` when present. The exact allowlist is owned by
`tools/package_generator.py`; other files cannot enter the archive implicitly.
Repository-only commands and demo paths in the documentation require a full
checkout. They are not dependencies of the packaged generator.

Run the extracted `tools/flowdata/gen.py` with Python 3.10 or newer. Generation
requires `flatc` in `>=24.3.25,<26.0.0`; Rust generation also requires `rustfmt`
on `PATH`. Compiler and runtime compatibility is described in the
[generator guide](../tools/flowdata/README.md).

The embedded `flowdata-source.json` has format version `1` and records:

- Generator name/version and entrypoint.
- Canonical upstream repository, full source commit, and release tag (or `null`
  for an untagged validation package).
- Generator tool requirements and the included license path, if any.
- Sorted relative paths, byte sizes, and SHA-256 hashes of every included file
  except the manifest itself.

This is not the generated-language `flowdata-manifest.json`. Keep both identities
distinct: one describes generator sources, the other describes generated SDKs.
The checksum sidecar covers the final archive, including its source manifest.
Checksums detect changes; they are not signatures or a grant of redistribution
rights. Obtain expected checksums from a trusted release and pin them downstream.

## Development And Release Versions

The generator version is a string in `tools/flowdata/engine/version.py`.
It supports stable `MAJOR.MINOR.PATCH` versions and the exact `-dev` suffix;
other prerelease suffixes and build metadata are not supported.

Start each release cycle with its planned version and the `-dev` suffix in a
reviewed commit. Generated SDK manifests and untagged source packages keep
that full version. Development source archives use names such as
`flowdata-sdk-generator-0.7.0-dev.tar.gz` and record their source commit with a
null tag. They can be built, verified, and smoke-tested locally.

When releasing, remove `-dev`, update the sanity suite's version assertions and
documented CLI output, and commit the finalized version before creating the
matching stable tag. Tags validate the source version; they do not assign it.
Tagged packages and publishing reject development versions, including packages
whose manifest has been relabeled with a release tag.

This lifecycle is separate from the generated SDK's mandatory `--version`, which
continues to accept only stable `MAJOR.MINOR.PATCH` values.

## Build And Verify Locally

The following tools are repository-only and are not shipped in the archive.
Commit the intended inputs before packaging: the builder reads only Git blobs
from `--ref` (default `HEAD`), never dirty or untracked working-tree files.

```bash
python3 tests/generator_package_test.py
python3 tests/generator_release_test.py
python3 tools/package_generator.py build --output-dir dist
python3 tools/package_generator.py verify dist/flowdata-sdk-generator-<version>.tar.gz
python3 tests/generator_package_test.py --archive dist/flowdata-sdk-generator-<version>.tar.gz
```

Replace `<version>` with the generator version. The builder refuses to overwrite
an archive or checksum; use a new output directory for another build.
Use `--ref <commit>` to select an older committed snapshot. With `--tag v<version>`,
the tag must already exist, match the generator version, and resolve to that
snapshot. Verification accepts `--commit <sha>` and `--tag v<version>` to enforce
expected release identity. A standalone `LICENSE` is not required for packaging
or publication; existing `LICENSE` and `NOTICE` files are included when present.

Packaging normalizes archive order, permissions, ownership, timestamps, and gzip
metadata. The same source commit and tag produce identical bytes independent of
the output directory and working-tree state. The source version comes only from
`tools/flowdata/engine/version.py`, not from the demo version or the application
SDK's `generate --version` argument.

## GitHub Workflow

`.github/workflows/release-generator.yml` runs only when a `v*` Git tag is pushed.
It validates release eligibility, runs all checks, then publishes a new GitHub
Release. The tag must match the generator's stable `vMAJOR.MINOR.PATCH` version.
Pull requests, branch pushes (including `main`), and manual dispatch do not run
this workflow. For an untagged local package, use the build and verification
commands above.

The test matrix uses Ubuntu 24.04 with Python 3.10 and 3.12 on FlatBuffers 25.9.23,
plus Python 3.12 on the minimum supported FlatBuffers 24.3.25. Both compilers use
checksum-verified source. Toolchain setup pins Rust 1.98.1 with neutral rustfmt
configuration, and Python test/build dependencies. The npm demo uses its checked-in
lockfile and inherits the selected exact Python FlatBuffers constraint for its own venv.
The complete existing sanity suite covers all four SDKs, C++ build
integrations, the Python bridge, wheel packaging, and cross-language compatibility.

Package tests cover committed inputs, byte reproducibility, archive integrity,
unsafe members, source identity, documentation links, and optional legal files. The
extracted-package test runs without Git metadata or Python site packages and
generates all four SDKs from independent schemas, including C++ bridge, CMake,
and Meson outputs. It verifies the resulting language manifests. Full consumer
builds are covered separately by the repository sanity suite.

Only the Python 3.12 / FlatBuffers 25.9.23 job uploads the verified
`generator-package` artifact.
Publication downloads those exact bytes and rechecks the checksum, source commit,
and tag. It does not rebuild or regenerate. Logs are uploaded on failure.
Default workflow permissions are read-only; only the publication job has
`contents: write`. Actions are pinned to full commits.

## Publication Approval

Publication is intentionally blocked until the repository owner completes these
steps. The consumer integration owns the file disclaimers and redistribution
policy; the producer workflow does not add them or require a standalone license.

1. Review the exact package contents and approve the consumer's redistribution
   and disclaimer policy before public vendoring.
2. Configure the `generator-release` GitHub environment with required reviewers
   and appropriate deployment restrictions. Protect `v*` tags against updates
   and deletion with a repository ruleset; publication cannot make tag mutation
   and release creation atomic.
3. Set the repository variable `FLOWDATA_RELEASE_APPROVED` to `true` only after
   that review. Both validation and publication check this gate for tag pushes.
4. Finalize the generator version through a reviewed change, removing `-dev`
   before release. Update the sanity suite's expected generator-version assertions
   and documented CLI output. No automatic version bump is performed by this
   workflow; ordinary feature branches share the release cycle's development version.
5. Ensure the reviewed changes are on `main`, then create and push the matching stable
   `vMAJOR.MINOR.PATCH` tag. Its commit must be reachable from `main`.
6. Approve the publication environment after all matrix jobs pass.

Publication rejects any existing release or draft for the tag, creates a new draft
targeted at the verified commit, and uploads and publishes using that draft's ID.
It rechecks the remote tag and existing releases before publishing. Existing
releases and assets are never overwritten. If an upload fails,
an incomplete draft can remain. Inspect it and remove only that unpublished draft
before retrying; never replace an already published release. Use a new version
for changes to published contents.

Releases and Actions artifacts inherit upstream repository access restrictions;
the workflow does not make this repository public. However, copying generator
sources into a public consumer repository makes those selected sources public.

## Consumer Vendoring

An authorized maintainer downloads a selected release and verifies its archive
checksum against a trusted value before extraction. With a producer checkout,
also use `tools/package_generator.py verify` to validate the exact inventory and
source identity before adding consumer-owned disclaimers. Preserve the extracted
layout and any included legal files when committing the sources into the consumer
repository.

Adding disclaimers changes source-file hashes. Keep `flowdata-source.json`
unchanged as the receipt for the original upstream package, and record separate
hashes for the decorated consumer files. Do not prepend comments to the JSON
manifest. Use file-type-appropriate disclaimer syntax without changing generator
behavior, and validate generation after applying the headers.

Record the release version, full upstream commit, and archive SHA-256 in the
consumer's dependency lock. Verify the embedded file inventory before accepting
updates. Keep one explicit maintainer-owned update path; normal consumer builds
should use the committed sources without private GitHub credentials or downloads.
Do not use the consumer repository's Git HEAD as the generator's upstream identity.

This workflow does not modify `amp-dev-forge` or remove its submodule. That is a
separate migration after the producer package and consumer policy are ready.
