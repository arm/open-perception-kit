---
sidebar_position: 15
sidebar_label: Release packages
---

# Release packages

The release workflow descriptions below document the legacy release process;
those workflows have not been migrated into this repository. In particular,
the legacy CI image and its post-publication verification lane are no longer
available. See [current CI workflows](../../.github/CI-README.md).

`development/meson.build` is the product-version authority. A stable
`MAJOR.MINOR.PATCH` version must have a non-empty matching `CHANGELOG.md`
section before a pull request can target `main`.

## Inputs

Packaging discovers model directories directly under `config/models/`. The
existing `opk-models` stage runs `scripts/download-models.py`; descriptor
`hfDownload` entries pin the repository, revision, and filename. Both packages
include the six ONNX models `cam-contact`, `gaze-detection`, `osnet_x0_25`,
`ultraface`, `yolo26`, and `yolov11`, plus the checked-in ExecuTorch `yolox`
model.

The dedicated read-only `HF_TOKEN` is an accepted release-CI dependency while
this repository and required model sources remain private. It is confined to
the existing `opk-models` artifact stage and is not included in release images
or archives. A future public transition requires anonymously readable model
sources and removal of the workflow secret references.

Package builds reuse the selected source's existing deployment lane. Native
x86_64 and Arm jobs build `opk-deployment-base`; its `opk-deployment-build`
parent owns the toolchain, installs the Dockerfile-pinned ONNX Runtime and
ExecuTorch Debian package, builds the runnable snapshot, and creates the
validated architecture tarball. The workflow publishes those same native image
digests as one multi-architecture
`ghcr.io/arm/open-perception-kit-deployment` image and copies the tarball from
each finished image. Hugging Face and ExecuTorch credentials are BuildKit
secrets and are not stored in image layers or published artifacts. Release
builds package ONNX Runtime 1.24.4 with its
required SONAME link, and statically link ExecuTorch into its operation module
without shipping ExecuTorch SDK files.

`opk-deployment-build` creates the architecture-neutral Perception SDK triplet
directly from the selected commit's checked-in, CI-validated SDK snapshot. The
release source commit is a scalar build input. The normal tracked generator
sources in `tools/flowdata-sdk` enter the Docker build context so packaging can
verify their local content hashes without Git metadata or generator execution.
Generator updates are manual source changes; CI does not fetch or update them.
The stage embeds the triplet under `share/opk/perception-sdk`
and checks its provenance against the release commit. The Arm snapshot job
also uploads that exact embedded triplet as the existing temporary
`opk-perception-sdk-input-*` or `opk-test-perception-sdk-input-*` Actions
artifact; it does not rebuild it. For OPK publication, the Arm build extracts
the verified Python wheel and packages the prepared Rust tree from that triplet
using its locked offline Cargo vendor directory. The release-only crate manifest
records FlatBuffers as a crates.io dependency so consumers do not look for it in
the private registry. The Arm build verifies the packaged crate and stages the
prepared source that produced it beside both language packages in
`opk-perception-sdk-input-*`. It recreates the retained crate from that source
with Cargo 1.85 and a clean sparse crates.io configuration so native publication
produces the same bytes. For stable release pushes, an early job on
`open-perception-kit-runner-ubuntu-x64` checks that the Cargo version is available on
the explicit eu02 route before any public release mutation. The existing
`self-hosted-ubuntu-latest-x64` Artifactory job then publishes the three OPK
archives and the unchanged wheel to `edge-ai-tooling.pypi`. After that job
succeeds, the physical runner uses Cargo's native publish protocol with the
existing anonymous principal for `edge-ai-tooling.cargo`. After native
publication, it waits for the registered crate and its anonymous ownership,
compares it byte-for-byte with the Arm build's package, and waits for the
matching sparse index checksum. The post-publication workflow then runs clean,
exact-pinned Cargo 1.85 consumers on x86_64 and ARM64 without FlatBuffers
generation. A red release must be restored to its pre-release state by the
release owner before retrying.
Public distribution must use authenticated, server-enforced immutable
publication instead. Manual publication generates a unique prerelease version,
places the wheel and crate beside the archives in its generic Artifactory
prerelease folder, and publishes the language packages to their registries.
The publisher initially attaches only the three product archives to the GitHub
Release. Post-publication validation later adds named report assets without
replacing the published product files.

The architecture tarballs keep their seven-model allowlist. The image is the
full existing deployment snapshot, including the resolved configuration, model,
pipeline, and demo-media inputs copied by the deployment lane. Configuration
for disabled backends may be present, but their operation modules, SDKs, and
runtimes are not installed by the release build.

## Event routing

Release validation and publication use four workflows:

| Event | `release-tests.yml` | `release-publication-tests.yml` | `release-packages.yml` | `release-post-publication.yml` |
| --- | --- | --- | --- | --- |
| Pull request to `main` | Builds temporary x86_64 and Arm snapshot images, runs their native offline integration smokes, and emits the validated archives plus embedded Perception wheel | Uploads the archives and wheel to disposable Artifactory and the archives to a draft GitHub Release, verifies them, and deletes them | Not run | Not run |
| Push to `main` | Not run | Not run | Builds all three archives, smoke-tests both architecture images, publishes their multi-architecture GHCR image, then publishes the archives to one `v<version>` GitHub release and generic Artifactory, the Perception wheel to Artifactory PyPI, and the Perception crate to Artifactory Cargo | Consumes every published package variant, runs the common full OPK smoke, RPi5 Playwright, release Sonar with the Playwright LCOV artifact, all eight Valgrind pipelines, and full Black Duck built-output, dependency, source, and snippet scans; attaches their reports to the GitHub release |
| Manual release validation | Resolves `source_ref`, builds temporary x86_64 and Arm snapshot images, runs their native offline integration smokes, and emits the validated archives plus embedded Perception wheel | Uploads the archives and wheel to disposable Artifactory and the archives to a draft GitHub Release, verifies them, and deletes them | Not run | Not run |
| Manual package publication | Not run | Not run | Resolves `source_ref`, derives a sortable prerelease version, regenerates and commits every version consumer, then publishes the full GHCR, GitHub prerelease, generic Artifactory, PyPI, and Cargo release set | Performs the same full package-consumer, OPK, RPi5 Playwright, Sonar, Valgrind, Black Duck, and report-attachment fan-out as a stable release |

After every completed package-publication run, `workflow_run` starts the
post-publication workflow. The publisher records an Actions receipt only after
all required destinations succeed. The receipt binds the source run, commit,
version, Artifactory folder, artifact SHA-256 values, and GHCR digest. The
downstream workflow accepts only a completed `release-packages.yml` run from
this repository and validates the publication receipt before fan-out. The same
workflow can be replayed manually with the completed publisher run ID.

The fan-out builds one run-tagged `opk-ci` image for the exact published commit.
The full Black Duck built-output, dependency, source, and snippet scans consume
that shared image. A separate release Valgrind job runs every enabled pipeline
plus the failed-start/retry regression and retains the complete findings as a
report without turning individual findings into a release gate.
RPi5 runs the existing three-browser Playwright smoke.
Playwright uploads its LCOV report; release Sonar cannot start until it has
downloaded and validated that report, then imports it alongside the native
coverage. Artifactory artifacts are downloaded from their published folder and
compared with the receipt. The published x86_64 archive is extracted and its
packaged GStreamer elements, inference pipelines, and web sink are exercised
offline. Manual prereleases install and import both the exact generic wheel and
the exact PyPI package; Python and Cargo consumers resolve the published
version. Stable Sonar uses `tags/v<version>` while manual prereleases use
`release-prereleases/<version>`, so a test publication cannot replace the stable
release analysis.
After every fan-out leg finishes, the workflow adds a Markdown job/result/time
summary and the available Valgrind, Playwright, and Black Duck report archives
to the existing GitHub Release and mirrors the same files below the release's
Artifactory `reports/` folder. Its generated release-notes block links those
assets, the exact Sonar branch, the Black Duck reports, and the workflow run.

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
After a hotfix adds or changes a downstream publication workflow on `main`,
back-merge it to `develop` before relying on the new trigger.

Each workflow resolves one immutable commit and uses it for every image build.
Manual full Black Duck runs in `opk-ci.yml` reuse `release-tests.yml` to build
candidate images and packages without publishing a release or changing a tag.
Candidate builds have read-only registry access. A manual-only job publishes the
exported images; it neither checks out nor executes the selected source.
Candidate and published inputs share `blackduck-artifacts.yml`; documentation
packaging is owned by `release-docs-build.yml`. See the
[Black Duck input contract](../../.github/CI-README.md#black-duck-inputs).
Push and manual publication cannot start unless both native release images
pass the same embedded integration smoke used for pull requests.
The native jobs import the nightly deployment lane's architecture-specific
BuildKit registry graph and fall back to its shared compiler cache. Release
builds do not export a second full BuildKit graph.
The native jobs push their existing image outputs by digest; one final manifest
combines those exact amd64 and arm64 digests without rebuilding. Stable releases
use the product version as the GHCR tag. Manual prereleases derive
`YYYYMMDD.1HHMMSS.<run-id-and-attempt>` and include the selected commit in their build label;
the generator updates every checked-in version consumer before publication.
Their GHCR tag also includes the workflow run ID and attempt. The workflow
summary and GitHub Release notes record the pullable reference and immutable
manifest digest.
Manual release validation emits only temporary Actions artifacts and activates
the same disposable publication probes; no uploaded package, release, or tag is
retained.
Artifactory depends on successful GitHub Release publication, so the
existing-version guard protects both release destinations. Manual releases are
marked as GitHub prereleases and also publish and verify the exact PyPI and Cargo
versions. Their Python package uses `<generated-version>.dev0` and their Cargo
crate uses `<generated-version>-dev.0`, so package managers do not select a
test publication as the latest stable SDK.

Automatic `main` archives are stored under `releases/<version>/`; manual
artifacts are stored under `prereleases/<generated-version>/` below
`https://artifactory.arm.com/artifactory/ai-expkits-internal.opk-ci`. The same
URL is used for uploads and generated download links. The publisher job uses
the locked `Arm-Debug/publisher` package from its synchronized runtime-only
environment, prints the three stable generic URLs or five prerelease URLs, and
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

Inside `opk-deployment-build`, the `scripts/build.sh release` build
enables Meson's package install surface. The same Docker stage installs that
build into a cacheable staging root, adds the resolved models and pinned
runtimes, stages the locked NumPy, FlatBuffers, and Perception Python packages,
and validates every ELF. A final identity-only layer names and archives that
payload with system `tar` and `gzip`; it does not call a release-build wrapper
or configure a second build tree.
The architecture archives expose only the GStreamer plugin integration
surface; neither Meson nor release scripts install OPK source headers.

The documentation job builds the existing `opk-docs` target, runs
`scripts/gen-doc.sh` inside it, and creates the documentation archive with
system `tar`. It does not install a second documentation toolchain or use a
release-specific packaging entrypoint.

The validator checks:

- exactly six GStreamer plugins;
- exactly four OPK operation modules, including `opk-python-ops.so`;
- one unversioned `lib/opk/opk-runtime.so`;
- the private Python runtime modules and distribution manifest;
- no public or source headers;
- expected architecture and package-relative RUNPATH on every OPK DSO;
- complete classified `DT_NEEDED` resolution;
- no fmt DSO, source, tests, examples, or pipeline presets;
- one ONNX Runtime binary and its `libonnxruntime.so.1` link;
- the standard, ONNX, and experimental ExecuTorch operation modules;
- exactly the seven release model directories;
- the exact clean Perception SDK ZIP and its checksum/provenance sidecars;
- the selected source's descriptor schemas under `share/opk/schemas/json/v1`;
- ExecuTorch and third-party legal documentation, with no ExecuTorch SDK files;
- local relative model and OpChain references.

The documentation archive is generated separately, so architecture packages
do not duplicate raw or generated documentation.

For every event path, the native x86_64 and Arm `opk-deployment-base` builds run
the same offline, non-root integration smoke inside the existing Dockerfile. It
extracts the generated archive and discovers its installed elements before
separate pipelines cover YOLov11 with ONNX Runtime and YOLOX with experimental
ExecuTorch. Both must reach EOS and emit non-empty output through `opkcomm`; a
final pipeline starts `opksink` from the package and its installed web root. The
validated archive is then published without rebuilding.
