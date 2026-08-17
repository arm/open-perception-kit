---
sidebar_position: 15
sidebar_label: Release packages
---

# Release packages

`development/meson.build` is the product-version authority. A stable
`MAJOR.MINOR.PATCH` version must have a non-empty matching `CHANGELOG.md`
section before a pull request can target `main`.

## Inputs

Packaging discovers model directories directly under `config/models/`. Each
workflow runs `scripts/download-models.py` once in its prepare job with the
repository `HF_TOKEN`; descriptor `hfDownload` entries pin the repository,
revision, and filename. The resolved model tree is passed to both architecture
builds as a short-lived Actions artifact. Both packages include the six ONNX
models `cam-contact`, `gaze-detection`, `osnet_x0_25`, `ultraface`, `yolo26`,
and `yolov11`, plus the checked-in ExecuTorch `yolox` model.

The dedicated read-only `HF_TOKEN` is an accepted release-CI dependency while
this repository and required model sources remain private. It is confined to
model resolution and is not passed to package builds or included in release
artifacts. A future public transition requires anonymously readable model
sources and removal of the workflow secret references.

Dependency preparation reuses the selected source's ONNX Runtime and
ExecuTorch Debian installers.
This keeps manual builds aligned with the source revision being packaged.
It deliberately does not reuse `deps/` or a development-container filesystem:
those inputs are architecture/profile-specific, may be stale, and may not
contain the runtime required by the release. Clean release jobs prepare the
selected commit's inputs independently for each architecture. Both release
workflows read the ONNX Runtime version and ExecuTorch version/revision from
the selected source's `Dockerfile` and pass them explicitly to dependency
preparation. APT resolves the native ExecuTorch package from those values, so
maintainers do not enter architecture identifiers, filenames, URLs, or hashes.
Preparation validates the installed ExecuTorch package and SDK, and collects
both dependencies' legal documentation. Package build jobs receive the
prepared runtimes and resolved model
files as isolated inputs with no Hugging Face, dependency, or publishing
credentials. Release builds omit NCNN and HailoRT, package ONNX Runtime 1.24.4
with its required SONAME link, and statically link ExecuTorch into its operation
module without shipping SDK files.

Each workflow resolves models once; both builds package the same seven-model
allowlist from that resolved tree. Other ONNX models and all Hailo models,
operation modules, backend SDKs, and runtimes are excluded.

Each workflow also builds the architecture-neutral Perception SDK triplet once
from the selected commit. The build initializes only the private
`tools/flowdata-sdk` submodule with the existing deploy key, verifies the ZIP
and both sidecars, and passes the same short-lived input artifact to both
architecture builds. The key is not available to package, smoke, or publication
jobs. Both packages embed the unchanged files under
`share/pek/perception-sdk/`; the triplet is not a top-level PEK release asset.

Product descriptor schemas are a separate outer-package input. Packaging copies
`config/schemas/v1` recursively to `share/pek/schemas/json/v1`, preserving file
paths and bytes. These JSON schemas are not added to the Perception SDK ZIP.

## Event routing

Release validation and publication use three workflows:

| Event | `release-tests.yml` | `release-publication-tests.yml` | `release-packages.yml` |
| --- | --- | --- | --- |
| Pull request to `main` | Builds temporary x86_64 and Arm candidates, runs both offline package smoke tests, and emits the tested archives | Uploads the tested archives to disposable Artifactory and draft GitHub Release locations, verifies them, and deletes them | Not run |
| Push to `main` | Not run | Not run | Builds all three archives, runs both offline package smoke tests, and publishes them to one `v<version>` GitHub release and Artifactory |
| Manual release validation | Resolves `source_ref`, builds temporary x86_64 and Arm candidates, runs both offline package smoke tests, and emits the tested archives | Uploads the tested archives to disposable Artifactory and draft GitHub Release locations, verifies them, and deletes them | Not run |
| Manual package publication | Not run | Not run | Resolves `source_ref`, builds all three archives, runs both offline package smoke tests, and publishes to Artifactory only |

Credentialed publication probes run only after an unprivileged pull-request or
manual validation workflow succeeds. The trusted `workflow_run` workflow does
not check out or execute the selected source; it accepts only the two
smoke-tested architecture archives. It uploads them with Publisher below
`ci/run-<source-run-id>-<attempt>/<commit>/`, verifies and always deletes that
folder. It also creates a draft prerelease titled
`[TEST ONLY - DO NOT USE]`, uploads and verifies both assets, then always
deletes the release and tag. The workflow reports a
`Release publication validation` status on the pull-request commit; it passes
only when both publication probes pass.

GitHub loads `workflow_run` definitions from the default `develop` branch.
After a hotfix adds or changes this probe on `main`, back-merge it to `develop`
before relying on the new validation for later release pull requests.

Each workflow resolves one immutable commit and uses it for every dependency,
build, and smoke job. Push and manual publication cannot start unless both
architecture archives pass the same package smoke test used for pull requests.
Manual release validation emits only temporary Actions artifacts and activates
the same disposable publication probes; no uploaded package, release, or tag is
retained.
For a push, Artifactory additionally depends on successful GitHub Release
publication, so the existing-version guard protects both release destinations.
Manual snapshots bypass the skipped GitHub Release job and continue to publish
only to Artifactory.

Automatic `main` archives are stored under `releases/<version>/`; manual
archives are stored under
`snapshots/<label>/<full-sha>-<run-id>-<attempt>/` below
`https://artifactory.arm.com/artifactory/ai-expkits-internal.opk-ci`. The same
URL is used for uploads and generated download links. The publisher job uses
the locked `Arm-Debug/publisher` package from its synchronized runtime-only
environment, prints all three final URLs, and adds links and SHA-256 values to
the workflow summary for both paths. Once this
workflow exists on the default `develop` branch, a manual run may select a
feature branch while the release process is being tested. GitHub does not
dispatch a new workflow before it has been registered on the default branch.

Cross-system publication is deliberately not resumed automatically. If the
GitHub Release succeeds and the following Artifactory upload fails, repair or
remove that GitHub Release before rerunning; otherwise its immutable version
guard rejects the rerun.

## Package validation

`scripts/release/BuildPackage.sh` installs only Meson's release surface into a
clean staging root, adds the resolved models and pinned runtimes,
validates every ELF, then creates the archive with system `tar` and `gzip`.
The architecture archives expose only the GStreamer plugin integration
surface; neither Meson nor release scripts install PEK source headers.

The validator checks:

- exactly six GStreamer plugins;
- one unversioned `lib/pek/pek-runtime.so`;
- no public or source headers;
- expected architecture and package-relative RUNPATH on every PEK DSO;
- complete classified `DT_NEEDED` resolution;
- no fmt DSO, NCNN, source, tests, examples, or pipeline presets;
- one ONNX Runtime binary and its `libonnxruntime.so.1` link;
- the standard, ONNX, and experimental ExecuTorch operation modules;
- exactly the seven release model directories and no Hailo content;
- an exact, clean, verified Perception SDK ZIP and sidecar triplet under
  `share/pek/perception-sdk/`;
- a safe, non-empty, parseable descriptor schema tree under
  `share/pek/schemas/json/v1`, matching the selected source before archiving;
- ExecuTorch and third-party legal documentation, with no SDK files;
- local relative model and OpChain references.

The documentation archive is generated separately, so architecture packages
do not duplicate raw or generated documentation.

For every event path, the native x86_64 and Arm jobs run
`scripts/release/SmokePackage.py` against the exact archives created by that
workflow. Each smoke uses PyGObject rather than GStreamer command-line tools:
it unsets `LD_LIBRARY_PATH`, loads the packaged private runtime, discovers
plugins through `GST_PLUGIN_PATH`, controls inference through EOS, and verifies
the packaged `peksink` web content. Separate inference runs cover YOLov11 with
ONNX Runtime and YOLOX with experimental ExecuTorch.
The extracted-package validation also verifies the embedded SDK triplet and
checks the direct schema tree structurally. Selected-source byte and commit
comparisons happen before archiving, when that source checkout is available.
Push and manual workflows publish those same tested archives without rebuilding
them.
