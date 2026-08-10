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
builds as a short-lived Actions artifact. Both packages include exactly
`cam-contact`, `gaze-detection`, `osnet_x0_25`, `ultraface`, `yolo26`, and
`yolov11`. All six use ONNX Runtime.

Dependency preparation reuses the selected source's ONNX Runtime installer.
This keeps manual builds aligned with the source revision being packaged.
It deliberately does not reuse `deps/` or a development-container filesystem:
those inputs are architecture/profile-specific, may be stale, and may not
contain the runtime required by the release. Clean release jobs prepare the
selected commit's verified inputs independently for each architecture. Both
release workflows read the ONNX Runtime version from the selected source's
`Dockerfile` and pass it explicitly to dependency preparation.
Preparation fails immediately if the workflow does not pass the value; the
preparation script has no implicit default. The downloaded ONNX Runtime package
is checksum-verified, and its notices are collected from the package. Package
build jobs receive the prepared runtime and resolved model
files as isolated inputs with no Hugging Face, dependency, or publishing
credentials. Release builds omit NCNN, package ONNX Runtime 1.24.4 with its
required SONAME link, and disable HailoRT.

Each workflow resolves models once; both builds package the same six-model
allowlist from that resolved tree. Other ONNX models and all Hailo models,
operation modules, SDKs, and runtimes are excluded.

## Event routing

Release validation and publication use three workflows:

| Event | `release-tests.yml` | `release-publication-tests.yml` | `release-packages.yml` |
| --- | --- | --- | --- |
| Pull request to `main` | Builds temporary x86_64 and Arm candidates, runs both offline package smoke tests, and emits the tested archives | Uploads the tested archives to disposable Artifactory and draft GitHub Release locations, verifies them, and deletes them | Not run |
| Push to `main` | Not run | Not run | Builds all three archives, runs both offline package smoke tests, and publishes them to one `v<version>` GitHub release and Artifactory |
| Manual dispatch | Not run | Not run | Resolves `source_ref`, builds all three archives, runs both offline package smoke tests, and publishes to Artifactory only |

Credentialed publication probes run only after the unprivileged pull-request
workflow succeeds. The trusted `workflow_run` workflow does not check out or
execute pull-request code; it accepts only the two smoke-tested architecture
archives. It uploads them with Publisher below
`ci/pr-<number>/<commit>/<run>-<attempt>/`, verifies and always deletes that
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
clean staging root, adds the resolved models and pinned ONNX Runtime,
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
- exactly the six release model directories and no Hailo content;
- local relative model and OpChain references.

The documentation archive is generated separately, so architecture packages
do not duplicate raw or generated documentation.

For every event path, the native x86_64 and Arm jobs run
`scripts/release/SmokePackage.py` against the exact archives created by that
workflow. Each smoke uses PyGObject rather than GStreamer command-line tools:
it unsets `LD_LIBRARY_PATH`, loads the packaged private runtime, discovers
plugins through `GST_PLUGIN_PATH`, controls inference through EOS, and verifies
the packaged `peksink` web content.
Push and manual workflows publish those same tested archives without rebuilding
them.
