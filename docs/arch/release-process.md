---
sidebar_position: 15
sidebar_label: Release packages
---
<!-- SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates -->


# Release packages

The [Release workflow](../../.github/workflows/release.yml) builds and publishes
OPK packages through a manual `workflow_dispatch` run. Merging a pull request
or pushing to `main` does not start a product release.

## Source and version

Run the workflow from `main`. It uses `github.sha` and the committed version,
with no custom dispatch inputs or generated prerelease versions. Package and
publication jobs use the same resolved commit SHA.

The final production-docs job requires the workflow ref to be `main` and the
source commit to be in `main`'s history. Selecting another branch or tag can
publish packages before failing this check.

[`ReleaseTool.py prepare`](../../scripts/release/ReleaseTool.py) validates these
checked-in inputs before packaging:

| Input | Requirement |
| --- | --- |
| `development/meson.build` | Product version in stable `MAJOR.MINOR.PATCH` form |
| `generated/open_perception_kit/python/pyproject.toml` | Python package version equals the product version |
| `generated/open_perception_kit/rust/Cargo.toml` | Cargo package version equals the product version |
| `CHANGELOG.md` | Non-empty `## [<version>]` section for the product version |

In a release-preparation PR, update the product version, changelog section, and
exact `open_perception_kit` dependency in `tools/plumber/pyproject.toml`.
Regenerate and check the SDK and WebUI:

```bash
./scripts/perception-sdk.sh generate
./scripts/opksink-web.sh generate
./scripts/perception-sdk.sh check
./scripts/opksink-web.sh check
./scripts/pre-commit/run.sh
```

Follow the repository's [release skill](../../.agents/skills/opk-release/SKILL.md)
for version selection and the remaining preparation checks. Commit the sources
and generated outputs together.

After the preparation PR merges and the selected source has passed its checks,
start **Actions → Release → Run workflow**, selecting `main`, or use:

```bash
gh workflow run release.yml --repo arm/open-perception-kit --ref main
```

This command starts publication.

## Builds and artifacts

Native Ubuntu 24.04 jobs build the `opk-deployment-base` target in the
[Dockerfile](../../Dockerfile) for `linux/amd64` and `linux/arm64`, then extract
the release packages from the local images.

Packaging stages only the JSON descriptors and OpChains from the twelve
selected directories under `config/models/`. Their `hfDownload` entries retain
the pinned repository, revision, filename, and SHA-256 for user downloads.
Model binaries are excluded from release archives, deployment images, and
Cairn images, and are not required to build those artifacts.

The existing quick-start development flow uses the `opk-models` stage and
`scripts/download-models.py`. The standard OPK model sources are public;
quick-start CI downloads the pinned artifacts anonymously, without an `HF_TOKEN`
secret. Release builds and smoke checks do not use `opk-models`, download models
or receive Hugging Face credentials. Optional authentication for custom private or gated models
remains available through the existing downloader; see
[Bring your model](../public/how-to/bring-your-model.md).

Users download models separately before running a release; see
[Download models](../public/getting-started/binary-release.md#download-models).
Deployment containers mount the user's downloaded model directory read-only.

The build verifies ExecuTorch package checksums and embeds the checked-in SDK
ZIP, checksum, and provenance under `share/opk/open-perception-kit`. It checks
SDK provenance against the source commit without regenerating the SDK.
The Arm job extracts the Python wheel and prepares the Rust crate for publication.

For product version `<version>`, the release produces:

| Artifact | Destination |
| --- | --- |
| `opk-<version>-linux-x86_64.tar.gz` | Actions, Artifactory, GitHub Release |
| `opk-<version>-linux-aarch64.tar.gz` | Actions, Artifactory, GitHub Release |
| `open_perception_kit-<version>-py3-none-any.whl` | Actions and Artifactory publisher input |
| `open_perception_kit-<version>.crate` | Actions, Artifactory publisher input, and Cargo publication |
| `opk-sbom-source.json` | Actions and GitHub Release |
| `opk-sbom-open-perception-kit-crate.json` | Actions and GitHub Release |

Both SBOMs use CycloneDX and include the Arm disclaimer. The SDK ZIP is embedded
in each architecture archive. Documentation is published as a site.

## Publication order

1. `prepare` validates the version and source commit. Native builds and source
   SBOM generation follow. Quick-start smoke, SDK tests, and Meson tests also run.
2. `artifactory` waits for both builds, the source SBOM, and all three test
   workflows. It collects exactly two archives, the wheel, and the crate in
   `opk-release-artifacts`, then calls `upload-to-artifactory.yaml` in
   `Arm-Debug/amp-dev-forge-publisher`. It waits for success and retrieves the
   published download URLs.
3. `perception-cargo-publish` rebuilds and byte-compares the crate, generates its
   SBOM, and calls `cargo-publish.yaml` in the same publisher repository.
   It waits for publication to succeed. The publisher owns registry credentials
   and destinations.
4. `github-release` waits for both publishers, rejects an existing `v<version>`
   release, and publishes the two archives and two SBOMs at the selected commit.
   Release notes include the changelog entry, source commit, archive SHA-256
   values, and Artifactory links.
5. `publish-docs` calls
   [Publish Docs (Production)](../../.github/workflows/docs-publish-production.yml)
   with the release commit to validate and publish the production site.

A push to `main` separately starts the
[staging documentation workflow](../../.github/workflows/docs-publish.yml).

Do not overwrite published versions. The existing-release check runs after
Artifactory and Cargo publication. A failed run can leave packages or a GitHub
Release behind, with no automatic rollback. Check completed jobs and publisher
runs before retrying.

## Package validation

[`ReleaseTool.py validate-package`](../../scripts/release/ReleaseTool.py) checks
the staged payload before archiving:

- Native plugins and runtimes, architecture, RUNPATH, and ELF dependencies.
- Private Python runtime and distribution manifest.
- Exactly twelve model descriptor directories and their local model/OpChain references, without model binaries.
- SDK ZIP, checksum, provenance, and descriptor schemas.
- Legal documents and exclusion of source headers and SDK build files.

Both architecture images run the same
[package smoke](../../scripts/release/smoke-opk-package.sh) in the
`opk-deployment-base` stage without network access, as a non-root user:

```bash
scripts/release/smoke-opk-package.sh \
  opk-<version>-linux-<architecture>.tar.gz python-operation.py
```

The smoke checks required licence evidence, plugin discovery, Python-operation
dependencies through an OpChain containing only the Python test Op, and `opksink`
startup. It needs no model files and performs no inference. Model-dependent
runtime tests remain in the development and quick-start test suites. The archive
remains unchanged and is published without rebuilding.
