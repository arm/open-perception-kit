---
sidebar_position: 4
sidebar_label: Containers
---
<!-- SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates -->


# Container Structure

OPK uses separate container image lanes for development, documentation,
pre-commit checks, and deployment. The structure keeps tool-heavy images away
from runtime images while still giving each workflow a reproducible environment.

[Current container structure PlantUML source](../plantuml/container-structure.puml)

## Architecture

The container graph is organized into four lanes. The arrows below mirror the
current Dockerfile `FROM` and artifact-copy relationships.

```text
External bases
  debian:trixie-slim
  python:3.13-slim-trixie

Shared base and artifact stages
  debian:trixie-slim
    -> opk-demo-media
  python:3.13-slim-trixie
    -> opk-build-base
       -> opk-cross-build-base
    -> opk-models

Development tooling lane
  opk-build-base
    -> opk-dev-base
       -> opk-dev-tools
          -> opk-dev
             -> opk-dev-macos-cache-build
             -> opk-dev-macos-ci
  opk-models
    --copy model artifacts--> opk-dev
  opk-demo-media
    --copy demo videos--> opk-dev

Documentation lane
  opk-dev-base
    -> opk-docs

Deployment lane
  opk-build-base
    -> opk-cross-build-base
    -> opk-deployment-build
  opk-demo-media
    --copy demo videos--> opk-deployment-build
  opk-deployment-build
    --copy selected /opt/opk-app, release archive, and ONNX Runtime outputs-->
  python:3.13-slim-trixie
    -> opk-gstreamer-runtime-base
       -> opk-python-ops-runtime
          -> opk-deployment-base

Helper lane
  python:3.13-slim-trixie
    -> opk-pre-commit-runtime
```

The main `Dockerfile` owns the cross-lane stage graph where tool stages inherit
from `opk-build-base`, `opk-cross-build-base`, or `opk-dev-base`. The same file
also contains narrow artifact stages for model downloads and demo media so
development and deployment can consume the same resolved inputs without
inheriting artifact-stage tools. The helper runtime uses a separate top-level
Dockerfile because it does not share the core Debian build graph.

Native Meson, Valgrind, and clang-tidy workflows use the shared
`.github/actions/setup-build` action. Quick-start smoke uses `opk-dev`.
Host pre-commit hooks build `Dockerfile.pre-commit` directly.

Binary release jobs build `opk-deployment-base` locally and export its validated
architecture archives. Users extract the matching architecture package and set
its plugin directory in `GST_PLUGIN_PATH`.

## Runtime Contracts

The image graph describes what each image contains. The runtime environment is
completed by Compose service selection and host-generated overrides, which are
part of the container contract as well.

### Device Passthrough

Before a development or quick-start container is created,
`.devcontainer/platform_init.sh` runs on the Docker host and calls
`scripts/private/dev-init.sh`. The initialization flow discovers cameras, audio
devices, shared memory, and DMA-related resources, then generates
the matching `.devcontainer/docker-compose.<kind>.*.yaml` overrides and
`devices.env` entries.

The selected Compose service, generated overrides, and `devices.env` together
define which host resources enter the container. Device discovery must stay on
the host because the container cannot discover resources that have not yet been
passed through.

### Host And Bridge Networking

Linux and remote Raspberry Pi development normally inherit host networking from
`compose.base.yaml`, so services such as the OPK web UI and documentation server
bind directly on the Docker host. WSL and macOS enable the checked-in TURN
override: the OPK and coturn services use bridge networking, required ports are
published, and WebRTC traffic can use the advertised host address and relay
port range. CI and narrow helper services also use bridge networking because
they do not need the development runtime's direct host service exposure.

Build networking is a separate setting from runtime `network_mode`; changing
one does not change the other.

### Browser Media Output

Direct display and audio forwarding from a container varies across Linux,
remote hosts, WSL, and macOS. Runtime presets therefore normally terminate in
`opksink`, which encodes the media and exposes browser playback and control over
WebRTC and HTTP. This keeps the normal headless and remote workflow independent
of host display forwarding and avoids a separate UDP media-output contract.

See [opksink](elements/opksink.md) for the element's media, signaling, control,
and lifecycle details. Its current application-boundary limitations are tracked
in [Known Limitations](known-limitations.md).

## Motivation

Development and CI containers need broad tooling: compilers, build systems,
formatters, test tools, coverage tools, and diagnostics. Deployment containers
need the opposite: a smaller runtime environment with only the packages and OPK
outputs required to run the application.

Keeping these concerns separate avoids turning the normal development image into
a hidden release artifact and avoids shipping compilers, source trees, temporary
build directories, or quality-check tooling in deployment images.

The intended contract is:

- Tool images provide repeatable environments for building, checking, and
generating artifacts from the current checkout.
- Artifact stages resolve shared inputs such as model files and demo media once,
then copy them into the images that need them.
- Deployment build stages compile and package OPK during image creation.
- Deployment runtime images copy only selected runtime outputs from deployment
build stages.
- Helper images stay narrow and support one workflow, such as pre-commit checks.

## Installed Tools By Layer

The lists below name the tools or runtime packages added by each layer. Child
stages inherit everything from their parent unless noted otherwise.

- `opk-build-base`: `ca-certificates`, `curl`, `git`, `build-essential`,
  `meson`, `ninja-build`, `pkg-config`, `cmake`, `unzip`, `python3` with venv,
  OpenSSL, fmt, FFTW, libsoup, JSON-GLib, and GStreamer development
  headers.
- `opk-cross-build-base`: currently inherits `opk-build-base` and gives the
  deployment build lane a named cross-build root.
- `opk-demo-media`: starts from `debian:trixie-slim`, adds `ca-certificates`,
  `curl`, and `bash`, then runs `scripts/private/download-demo-videos.sh` unless
  `NO_EXAMPLE_CONTENT=true`. The checked-in manifest locks each file from the
  Arm Multimedia Hugging Face bucket by SHA-256, and the stage emits
  `data/videos/SHA256SUMS` beside the verified media.
- `opk-models`: starts from `python:3.13-slim-trixie`, adds
  `huggingface_hub==1.18.0` and `jsonschema==4.26.0`, then runs
  `scripts/download-models.py` with the optional Hugging Face build secret to
  resolve model artifacts under `config/models`.
- `opk-dev-base`: adds `wget`, `sudo`, `gnupg`, `shfmt`, `zip`, `python3-pip`,
  `pre-commit`, `lldb-17`, `valgrind`, `ccache`, `file`, GStreamer runtime plugins,
  `actionlint`, ONNX Runtime, `uv`, the `opk-ci` tool, `plumber`, and
  `huggingface_hub==1.18.0` in the devtools venv, with
  `jsonschema==4.26.0` inherited from its system-site packages. It also owns
  the shared mounted-checkout entrypoint used by development and CI targets.
- `opk-dev-tools`: adds ExecuTorch packages, locale support, shell/editor tools
  such as `zsh`, Vim, Neovim, Nano, tmux, bash completion, `mc`, debugging and
  language tools such as `gdb` and `clangd`, browser and device tools such as
  Firefox ESR and `v4l-utils`, search/navigation tools such as `ripgrep`,
  `fd-find`, `eza`, and `bat`, Lua/LuaRocks/tree-sitter support, network
  diagnostics such as `iproute2`, `ping`, `traceroute`, `arping`, DNS tools,
  `tcpdump`, and `nmap`, plus VS Code C++ tools and Oh My Zsh setup.
- `opk-dev`: adds optional Raspberry Pi camera packages when `OPK_PICAMERA` is
  enabled, ensures an ARM64 ONNX Runtime path is available, and copies resolved
  model artifacts and demo videos into `/opt/opk-app` for first-run seeding. It
  does not copy the repository or prebuilt OPK binaries into the image.
- `opk-docs`: adds `openjdk-25-jdk`, Graphviz, Pandoc, Doxygen, and the
  PlantUML JAR.
- `opk-deployment-build`: inherits `opk-cross-build-base`, adds the target
  sysroot when cross-building, installs target ONNX Runtime, downloads Meson
  subprojects, copies model configurations from the checkout and demo
  videos from `opk-demo-media`, builds OPK release outputs, and collects
  `/opt/opk-app`. Native release builds install the ExecuTorch toolchain and
  enable the Python operation module for the runnable deployment image. They use
  the selected repository commit and local content hashes of the normal tracked
  `tools/flowdata-sdk` sources to verify and package the checked-in open-perception-kit
  snapshot without Git metadata. Generator sources are copied beside
  `tools/perception` and updated manually, not fetched during the build.
  Release builds reuse the same Meson build to create
  the validated architecture tarball in `/opt/opk-release-artifacts`. The native
  archive contains the Python operation module and copies its locked runtime
  packages into `share/opk/python`. Cross builds omit the embedded Python
  operation module because its target Python development dependency cannot be
  discovered through the current cross file.
- `opk-python-ops-runtime`: runs on the target platform and creates the embedded
  Python virtual environment from the pinned target-architecture NumPy wheel,
  the pinned FlatBuffers wheel, and the generated Open Perception Kit Python package.
- `opk-deployment-base`: contains only the selected deployment outputs and
  runtime dependencies: OpenSSL, fmt, FFTW, libsoup, JSON-GLib,
  libusb, zlib, GStreamer runtime/tools/plugins, optional Raspberry Pi camera
  packages, ONNX Runtime libraries, the target-platform Python operation
  runtime, the OPK app, and any release tarball copied from
  `opk-deployment-build`.
- `opk-pre-commit-runtime`: starts from `python:3.13-slim-trixie` and adds
  `ca-certificates`, `curl`, `git`, `shfmt`, `actionlint`, and `opk-ci`.

Quick-start builds mount a compiler cache at `$OPK_PROJECT_ROOT/.cache/ccache`.
On macOS, the Compose override selects `opk-dev-macos-ci` and mounts project-scoped
compiler cache and build output volumes. The container entrypoint maps the host
UID/GID and seeds the compiler cache from `/opt/opk-ccache`, which is populated
by `opk-dev-macos-cache-build`.

## Image Lanes

The development lane starts from shared native build tooling and adds the normal
interactive development environment. The final `opk-dev` image is for working in
a mounted checkout. It may carry resolved model artifacts for first-run setup,
but it should contain tools and dependency libraries, not a prebuilt copy of OPK
from the repository.

CI jobs build and test the checked-out source at job runtime. There is no
separate general-purpose CI image. The `opk-ci` Python tool remains installed
in the development and pre-commit images.

The `opk-docs` image reuses the development base and adds documentation tools
such as Doxygen, Pandoc, Graphviz, and PlantUML. For release documentation
publishing, see [Publish From GitHub Actions](../README.md#publish-from-github-actions).

The deployment lane has two roles plus shared artifact inputs.
`opk-deployment-build` inherits the cross-build base, copies model configurations
and demo media, compiles OPK, and collects `/opt/opk-app`. For a native release
it also packages the checked-in Open Perception Kit snapshot and creates the
architecture archive. `opk-python-ops-runtime` creates the Python environment
on the target platform. `opk-deployment-base` is the runnable deployment image
that receives the application and archive from the builder and the Python
environment from the target runtime stage.

The helper lane contains `opk-pre-commit-runtime`, which runs local repository
checks from the host Git hook.

## Use Cases

Use the development images for interactive work, local builds, debugging, and
running the kit from a mounted checkout.

Use the development container for local tests, Valgrind, and `opk-ci` commands.
See the [CI workflow definitions](../../.github/workflows/) for the standalone CI checks.

Use the documentation image for local generation of public docs, Doxygen output,
and PlantUML diagrams.

Use the deployment build and runtime images when producing a runnable deployment
image. This lane combines model configurations, demo media, compiled OPK
outputs, and runtime libraries. Users supply downloaded model files through a
runtime mount. It is the only lane where OPK binaries should be
built into an image as part of the image creation process.

Use helper images for narrow automation that does not need the full development
or CI environment. The pre-commit runtime is one example: the host hook invokes a
small container that runs repository checks against the mounted checkout.
