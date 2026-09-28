---
sidebar_position: 15
sidebar_label: Release packages
---

# Release packages

The [Release workflow](../../.github/workflows/release.yml) builds and publishes
OPK packages through a manual `workflow_dispatch` run. Merging a pull request
or pushing to `main` does not start a product release.

## Source and version

Select the workflow's branch or tag when starting the run. There are no custom
dispatch inputs: no `source_ref`, version override, or prerelease switch.
The `prepare` job checks out `github.sha`, resolves the full commit SHA, and
passes that immutable commit to the package and publication jobs. The workflow
does not create a version-update commit or generate a timestamped version.

Use `main` for the release flow. The final production-docs job requires the
manual run's workflow ref to be `main` and the selected source commit to be in
`main`'s history. Selecting another branch or tag can publish packages and then
fail that documentation check.

[`ReleaseTool.py prepare`](../../scripts/release/ReleaseTool.py) validates these
checked-in inputs before packaging:

| Input | Requirement |
| --- | --- |
| `development/meson.build` | Product version in stable `MAJOR.MINOR.PATCH` form |
| `generated/open_perception_kit/python/pyproject.toml` | Python package version equals the product version |
| `generated/open_perception_kit/rust/Cargo.toml` | Cargo package version equals the product version |
| `CHANGELOG.md` | Non-empty `## [<version>]` section for the product version |

Prepare a new version in a release-preparation PR before dispatching. Update
the product version, matching changelog section, and exact
`open_perception_kit` dependency in `tools/plumber/pyproject.toml`, then regenerate
the SDK and WebUI version consumers:

```bash
./scripts/perception-sdk.sh generate
./scripts/opksink-web.sh generate
./scripts/perception-sdk.sh check
./scripts/opksink-web.sh check
./scripts/pre-commit/run.sh
```

Follow the repository's [release skill](../../.agents/skills/opk-release/SKILL.md)
for version selection and the complete preparation checks. Commit the prepared
sources and generated outputs together. Ordinary feature, fix, and documentation
PRs do not need a new release version merely because they target `main`.

After the preparation PR merges and the selected source has passed its checks,
start **Actions → Release → Run workflow**, selecting `main`, or use:

```bash
gh workflow run release.yml --repo arm/open-perception-kit --ref main
```

This starts publication, not a validation-only run.

## Builds and artifacts

The native `ubuntu-24.04` and `ubuntu-24.04-arm` jobs build the existing
`opk-deployment-base` Docker target for `linux/amd64` and `linux/arm64`.
`OPK_RELEASE_BUILD=true`, `OPK_RELEASE_VERSION`, and
`OPK_RELEASE_SOURCE_COMMIT` select release packaging in the
[Dockerfile](../../Dockerfile). The images are loaded locally for extraction;
this workflow does not publish a GHCR image.

The existing `opk-models` stage owns model downloads through
`scripts/download-models.py` and the descriptors' pinned `hfDownload` entries.

The standard OPK model sources are public. Release and quick-start CI download
the pinned artifacts anonymously and do not require an `HF_TOKEN` secret.
Release users run the packaged model files without Hugging Face credentials or
network access. Optional authentication for custom private or gated models is
described in [Bring your model](../public/how-to/bring-your-model.md).

ExecuTorch Debian packages are downloaded from the configured artifact
endpoint and checked against architecture-specific SHA-256 values before the
Docker build.

`opk-deployment-build` installs the native release build into a staging root,
adds the model and runtime payloads, and validates the package. It packages
the checked-in SDK snapshot without regenerating it, embedding its ZIP,
checksum, and provenance sidecars under `share/opk/open-perception-kit`.
The SDK provenance is checked against the selected source commit.

The Arm job extracts that embedded SDK, copies its Python wheel, and prepares
the Rust crate and publication source with Cargo 1.85.0. It first packages with
the locked offline dependencies, then packages the publication source with a
clean sparse crates.io configuration. The publication job later recreates that
crate and compares its bytes before handing the source to the Cargo publisher.

For product version `<version>`, the release produces:

| Artifact | Destination |
| --- | --- |
| `opk-<version>-linux-x86_64.tar.gz` | Actions, Artifactory, GitHub Release |
| `opk-<version>-linux-aarch64.tar.gz` | Actions, Artifactory, GitHub Release |
| `open_perception_kit-<version>-py3-none-any.whl` | Actions and Artifactory publisher input |
| `open_perception_kit-<version>.crate` | Actions, Artifactory publisher input, and Cargo publication |
| `opk-sbom-source.json` | Actions and GitHub Release |
| `opk-sbom-open-perception-kit-crate.json` | Actions and GitHub Release |

The source and crate SBOMs use CycloneDX and include the Arm disclaimer. The SDK
ZIP remains embedded in the architecture archives; documentation is published
as a site rather than a third release archive.

## Publication order

1. `prepare` validates the release identity. The native builds and source SBOM
   generation use that identity. The workflow also calls the quick-start smoke,
   SDK tests, and Meson tests workflows.
2. `artifactory` waits for preparation, both builds, the source SBOM, and all
   three test workflows. It accepts exactly the two architecture archives,
   wheel, and crate into the `opk-release-artifacts` Actions artifact.
3. That job dispatches `upload-to-artifactory.yaml` on `main` in
   `Arm-Debug/amp-dev-forge-publisher`, passing the source run ID, artifact name,
   and `v<version>`. It waits for success and retrieves the publisher's
   `artifactory-links` artifact as the authoritative download URLs.
4. `perception-cargo-publish` verifies the crate, generates its SBOM, and
   dispatches `cargo-publish.yaml` in the same publisher repository with the
   source run ID, SDK artifact name, and Cargo version. It waits for that run
   to succeed. Registry credentials and destination handling belong to the
   publisher workflows.
5. `github-release` waits for Artifactory and Cargo publication. It requires
   two architecture archives and two SBOMs, rejects an existing GitHub Release
   named `v<version>`, creates a draft release at the selected commit, uploads
   those four assets, and publishes the release. Notes use the matching
   changelog section and include the commit, archive SHA-256 values, and
   Artifactory links.
6. `publish-docs` calls
   [Publish Docs (Production)](../../.github/workflows/docs-publish-production.yml)
   with the exact release commit. That workflow verifies membership in `main`,
   validates the public docs, and publishes the production site.

The OPK workflow delegates external registry uploads to the publisher
repository; it has no separate PyPI publication job. It also has no downstream
post-publication test/report workflow. A push to `main` separately starts the
[staging documentation workflow](../../.github/workflows/docs-publish.yml).
The production workflow's direct `release` event job is skipped; the explicit
call from `release.yml` owns release documentation publication.

Do not overwrite published versions. The existing-GitHub-Release check occurs
after Artifactory and Cargo publication, and publication across systems is not
atomic. A failed run can leave uploaded packages or a draft/published release.
Inspect the completed jobs and publisher runs and resolve partial publication
before retrying; the workflow does not roll it back automatically.

## Package validation

[`ReleaseTool.py validate-package`](../../scripts/release/ReleaseTool.py) checks
the staged payload before it is archived, including:

- six GStreamer plugins, four operation modules, and `opk-runtime.so`;
- the private Python runtime and its distribution manifest;
- the expected architecture, package-relative RUNPATH, and resolved ELF dependencies;
- ONNX Runtime and its SONAME link, plus the ExecuTorch operation module;
- the twelve allowed model directories and their local model/OpChain references;
- the SDK ZIP, checksum, provenance, and selected source's descriptor schemas;
- required legal documentation and exclusion of public/source headers and SDK build files.

Both architecture images run the same
[package smoke](../../scripts/release/smoke-opk-package.sh) without network
access and as a non-root user. It extracts the archive, discovers the installed
plugins, runs YOLO26n-320 and UltraFace with ONNX Runtime and NITEC with
ExecuTorch, checks packaged Python-operation dependencies, and starts `opksink`.
The workflow extracts and publishes that validated archive without rebuilding.
