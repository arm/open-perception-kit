---
sidebar_position: 15
sidebar_label: Release packages
---
<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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


# Release packages

The [Release workflow](../../.github/workflows/release.yml) builds and publishes
OPK packages through a manual `workflow_dispatch` run. Merging a pull request
or pushing to `main` does not start a product release.

## Licence evidence before publication

`ReleaseTool.py stage-legal` collects original notices from the resolved Meson
sources, ONNX Runtime, the ExecuTorch SDK's existing legal tree, installed Python
wheel metadata and the checked-in browser assets. It checks the collected
versions against `scripts/release/third-party-licenses.json`; new or upgraded
components require a catalogue update after their licences and original notices
have been reconciled. The catalogue records the source directory/revision for
Meson dependencies and the installed Debian package version for ExecuTorch.
`stage-legal` writes `components.json` with the revision/version, repository,
licence and notice paths, plus a sorted `THIRD_PARTY_LICENSES.md` report for the
OPK release. The same collection is
copied into architecture packages, deployment images and Cairn images.
`validate-package`, the offline archive smoke, and image `validate-legal` checks
reject missing or empty required notices and a missing or stale report.
Architecture packages and images require OPK's `LICENSE`, `NOTICE`, `THIRD_PARTY_NOTICE.md` and
licence-page copy. Deployment validation also requires NumPy and FlatBuffers
Python notices; Cairn uses the native component set. Architecture archives
additionally require the ExecuTorch notices.
SDK bundle verification also requires
the licence page, `LICENSE` and `NOTICE`.

Before publication, reconcile [the supplied OPK SBOM](https://confluence.arm.com/spaces/edgeaiexpkits/pages/3186688252/OPK+SBOM+%E2%80%94+a096f677d)
against the actual release inventory. That page's scan references revision
`8d8d38bf8d1513b197ce6fedf1c615f1e7731e3d`, with a 2026-09-21 results snapshot;
it does not describe every dependency at the current source revision. Known
differences include cpp-httplib 0.56.0 and ONNX Runtime 1.24.4. The pinned stb
source supplies MIT OR Unlicense terms, resolving its unknown entry for this
inventory. ExecuTorch and Font Awesome require additional evidence beyond
that scan. Preserve transitive notices supplied with the actual backend binaries
and runtime wheels rather than copying the scan's version list as release truth.

Record the required IP review link in [EXPKITS-1229](https://jira.arm.com/browse/EXPKITS-1229)
before publication. Review licences outside Arm's compatibility table and any
cryptography/trade-compliance questions through the process linked from
[EXPKITS-1388](https://jira.arm.com/browse/EXPKITS-1388). The supplied scan is
marked "Licence review required"; successful technical checks do not replace
that review. The Concordia GitLab example requires access that was unavailable
during this implementation; this inventory follows the Arm notice requirements.

### Reference strategy

The public [CMSIS Solution extension](https://github.com/Open-CMSIS-Pack/vscode-cmsis-solution/tree/1e71906aa81d3a018b0d30abe7e7e09e0017d765)
provides the reference for OPK's licence overview, automated source-header checks,
and dependency reports. Its `LICENSE` links to the dependency report and retains
the full Apache text; `LICENSE-Apache-2.0` provides a standalone copy. Its
`scripts/update-tpip.ts` flags new dependencies and changed licence metadata for
manual attention, and `scripts/tpip-reporter.ts` produces a release-versioned
Markdown report. The released `v1.72.0` Linux x64 VSIX was also inspected: it
contains these reports, the licence files and extracted JavaScript bundle notices.

OPK implements these practices through its existing `opk-ci` checks and
`ReleaseTool.py` collector. It uses Arm's combined copyright, SPDX and Apache
short-notice header and the required `perception-fdbck@arm.com` contact.
Copyright years reflect actual
contributions; upstream ownership is preserved. OPK keeps full original
dependency texts available offline, including backend transitive notices, and
uses `LICENSES/Apache-2.0.txt` for its standalone licence text. See the
[contribution rules](../../.github/CONTRIBUTING.md#copyright-and-licence-notices).

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
The shared `opk-release-sources` Docker stage excludes each descriptor's
`modelFile` path and its `.part` file before copying build inputs. This covers
custom filenames regardless of extension; `.dockerignore` also excludes known
model formats. Deployment and Cairn copies use those filtered inputs.

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
