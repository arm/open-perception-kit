---
sidebar_position: 15
sidebar_label: Release packages
---

# Release packages

`development/meson.build` is the product-version authority. A stable
`MAJOR.MINOR.PATCH` version must have a non-empty matching `CHANGELOG.md`
section before a pull request can target `main`.

## Inputs

Packaging discovers model directories directly under `config/models/`. The
existing `pek-models` stage runs `scripts/download-models.py`; descriptor
`hfDownload` entries pin the repository, revision, and filename. Both packages
include the six ONNX models `cam-contact`, `gaze-detection`, `osnet_x0_25`,
`ultraface`, `yolo26`, and `yolov11`, plus the checked-in ExecuTorch `yolox`
model.

The dedicated read-only `HF_TOKEN` is an accepted release-CI dependency while
this repository and required model sources remain private. It is confined to
the existing `pek-models` artifact stage and is not included in release images
or archives. A future public transition requires anonymously readable model
sources and removal of the workflow secret references.

Package builds reuse the selected source's existing deployment lane. Native
x86_64 and Arm jobs build `pek-deployment-base`; its `pek-deployment-build`
parent owns the toolchain, installs the Dockerfile-pinned ONNX Runtime and
ExecuTorch Debian package, builds the runnable snapshot, and creates the
validated architecture tarball. The workflow publishes those same native image
digests as one multi-architecture
`ghcr.io/arm-debug/amp-dev-forge-deployment` image and copies the tarball from
each finished image. Hugging Face and ExecuTorch credentials are BuildKit
secrets and are not stored in image layers or published artifacts. Release
builds package ONNX Runtime 1.24.4 with its
required SONAME link, and statically link ExecuTorch into its operation module
without shipping ExecuTorch SDK files.

`pek-deployment-build` creates the architecture-neutral Perception SDK triplet
directly from the selected commit's checked-in, CI-validated SDK snapshot. The
release source and `tools/flowdata-sdk` gitlink SHAs are scalar build inputs, so
release jobs neither initialize the private submodule nor exchange a parallel
SDK build input. The stage embeds the triplet under `share/pek/perception-sdk`
and checks its provenance against the release commit. The Arm snapshot job
also uploads that exact embedded triplet as the existing temporary
`pek-perception-sdk-input-*` or `pek-test-perception-sdk-input-*` Actions
artifact; it does not rebuild it. For PEK publication, the Arm build extracts
the verified Python wheel and packages the prepared Rust tree from that triplet
using its locked offline Cargo vendor directory. The release-only crate manifest
records FlatBuffers as a crates.io dependency so consumers do not look for it in
the private registry. The Arm build verifies the packaged crate and stages the
prepared source that produced it beside both language packages in
`pek-perception-sdk-input-*`. It recreates the retained crate from that source
with Cargo 1.85 and a clean sparse crates.io configuration so native publication
produces the same bytes. For stable release pushes, an early job on
`amp-dev-forge-runner-ubuntu-x64` checks that the Cargo version is available on
the explicit eu02 route before any public release mutation. The existing
`self-hosted-ubuntu-latest-x64` Artifactory job then publishes the three PEK
archives and the unchanged wheel to `edge-ai-tooling.pypi`. After that job
succeeds, the physical runner uses Cargo's native publish protocol with the
existing anonymous principal for `edge-ai-tooling.cargo`. After native
publication, it waits for the registered crate and its anonymous ownership,
compares it byte-for-byte with the Arm build's package, and waits for the
matching sparse index checksum. Clean, exact-pinned Cargo 1.85 consumer
builds then run on x86_64 and ARM64 without FlatBuffers generation. A red
release must be restored to its pre-release state by the release owner before
retrying.
Public distribution must use authenticated, server-enforced immutable
publication instead. Manual snapshots place the wheel and crate beside those
archives in their immutable generic Artifactory snapshot folder. GitHub Release
assets remain the three archives.

The architecture tarballs keep their seven-model allowlist. The image is the
full existing deployment snapshot, including the resolved configuration, model,
pipeline, and demo-media inputs copied by the deployment lane. Configuration
for disabled backends may be present, but their operation modules, SDKs, and
runtimes are not installed by the release build.

## Event routing

Release validation and publication use three workflows:

| Event | `release-tests.yml` | `release-publication-tests.yml` | `release-packages.yml` |
| --- | --- | --- | --- |
| Pull request to `main` | Builds temporary x86_64 and Arm snapshot images, runs their native offline integration smokes, and emits the validated archives plus embedded Perception wheel | Uploads the archives and wheel to disposable Artifactory and the archives to a draft GitHub Release, verifies them, and deletes them | Not run |
| Push to `main` | Not run | Not run | Builds all three archives, smoke-tests both architecture images, publishes their multi-architecture GHCR image, then publishes the archives to one `v<version>` GitHub release and generic Artifactory, the Perception wheel to Artifactory PyPI, and the Perception crate to Artifactory Cargo |
| Manual release validation | Resolves `source_ref`, builds temporary x86_64 and Arm snapshot images, runs their native offline integration smokes, and emits the validated archives plus embedded Perception wheel | Uploads the archives and wheel to disposable Artifactory and the archives to a draft GitHub Release, verifies them, and deletes them | Not run |
| Manual package publication | Not run | Not run | Resolves `source_ref`, builds all three archives, smoke-tests both architecture images, publishes their multi-architecture GHCR snapshot, and publishes the archives, Perception wheel, and Perception crate only to an immutable generic Artifactory snapshot folder |

On a push to `main`, release Sonar analysis and the staging docs deployment run
as independent release-package jobs. Their failures make the release workflow
red, but the GitHub Release and Artifactory publication jobs do not depend on
them and continue. The release Sonar image reuses and updates a dedicated
BuildKit registry cache in GHCR.

Credentialed publication probes run only after an unprivileged pull-request or
manual validation workflow succeeds. The trusted `workflow_run` workflow does
not check out or execute the selected source; it accepts only the two archives
and wheel produced by the smoke-tested architecture images. It uploads all three
with Publisher below `ci/run-<source-run-id>-<attempt>/<commit>/`, verifies and
always deletes that folder. It also creates a draft prerelease titled
`[TEST ONLY - DO NOT USE]`, uploads and verifies both assets, then always
deletes the release and tag. The workflow reports a
`Release publication validation` status on the pull-request commit; it passes
only when both publication probes pass.

GitHub loads `workflow_run` definitions from the default `develop` branch.
After a hotfix adds or changes this probe on `main`, back-merge it to `develop`
before relying on the new validation for later release pull requests.

Each workflow resolves one immutable commit and uses it for every image build.
Push and manual publication cannot start unless both native snapshot images
pass the same embedded integration smoke used for pull requests.
The native jobs import the nightly deployment lane's architecture-specific
BuildKit registry graph and fall back to its shared compiler cache. Release
builds do not export a second full BuildKit graph.
The native jobs push their existing image outputs by digest; one final manifest
combines those exact amd64 and arm64 digests without rebuilding. Stable releases
use the product version as the GHCR tag. Manual snapshots append the workflow
run ID and attempt to their build ID. The workflow summary and stable GitHub
Release notes record the pullable reference and immutable manifest digest.
Manual release validation emits only temporary Actions artifacts and activates
the same disposable publication probes; no uploaded package, release, or tag is
retained.
For a push, Artifactory additionally depends on successful GitHub Release
publication, so the existing-version guard protects both release destinations.
Manual snapshots bypass the skipped GitHub Release job and continue to publish
only to Artifactory.

Automatic `main` archives are stored under `releases/<version>/`; manual
artifacts are stored under
`snapshots/<label>/<full-sha>-<run-id>-<attempt>/` below
`https://artifactory.arm.com/artifactory/ai-expkits-internal.opk-ci`. The same
URL is used for uploads and generated download links. The publisher job uses
the locked `Arm-Debug/publisher` package from its synchronized runtime-only
environment, prints the three stable generic URLs or five snapshot URLs, and
adds links and SHA-256 values to the workflow summary. Stable crates are
published below
`https://artifactory.arm.com/artifactory/edge-ai-tooling.cargo/crates/perception/`.
Once this
workflow exists on the default `develop` branch, a manual run may select a
feature branch while the release process is being tested. GitHub does not
dispatch a new workflow before it has been registered on the default branch.

Cross-system publication is deliberately not resumed automatically. If the GHCR
image or GitHub Release succeeds and a later generic Artifactory, Cargo, or PyPI
publication fails, repair or remove the partial publications before rerunning.
The Cargo preflight rejects a version that is already visible, but the current
internal repository does not provide atomic overwrite protection.

## Package validation

Inside `pek-deployment-build`, the `scripts/build.sh release` build
enables Meson's package install surface. The same Docker stage installs that
build into a cacheable staging root, adds the resolved models and pinned
runtimes, stages the locked NumPy, FlatBuffers, and Perception Python packages,
and validates every ELF. A final identity-only layer names and archives that
payload with system `tar` and `gzip`; it does not call a release-build wrapper
or configure a second build tree.
The architecture archives expose only the GStreamer plugin integration
surface; neither Meson nor release scripts install PEK source headers.

The documentation job builds the existing `pek-docs` target, runs
`scripts/gen-doc.sh` inside it, and creates the documentation archive with
system `tar`. It does not install a second documentation toolchain or use a
release-specific packaging entrypoint.

The validator checks:

- exactly six GStreamer plugins;
- exactly four PEK operation modules, including `pek-python-ops.so`;
- one unversioned `lib/pek/pek-runtime.so`;
- the private Python runtime modules and distribution manifest;
- no public or source headers;
- expected architecture and package-relative RUNPATH on every PEK DSO;
- complete classified `DT_NEEDED` resolution;
- no fmt DSO, source, tests, examples, or pipeline presets;
- one ONNX Runtime binary and its `libonnxruntime.so.1` link;
- the standard, ONNX, and experimental ExecuTorch operation modules;
- exactly the seven release model directories;
- the exact clean Perception SDK ZIP and its checksum/provenance sidecars;
- the selected source's descriptor schemas under `share/pek/schemas/json/v1`;
- ExecuTorch and third-party legal documentation, with no ExecuTorch SDK files;
- local relative model and OpChain references.

The documentation archive is generated separately, so architecture packages
do not duplicate raw or generated documentation.

For every event path, the native x86_64 and Arm `pek-deployment-base` builds run
the same offline, non-root integration smoke inside the existing Dockerfile. It
extracts the generated archive and discovers its installed elements before
separate pipelines cover YOLov11 with ONNX Runtime and YOLOX with experimental
ExecuTorch. Both must reach EOS and emit non-empty output through `pekcomm`; a
final pipeline starts `peksink` from the package and its installed web root. The
validated archive is then published without rebuilding.
