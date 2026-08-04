---
sidebar_position: 4
sidebar_label: Containers
---

# Container Structure

OPK uses separate container image lanes for development, CI, documentation,
pre-commit checks, and deployment. The structure keeps tool-heavy images away
from runtime images while still giving each workflow a reproducible environment.

[Current container structure PlantUML source](../plantuml/container-structure.puml)

## Architecture

The container graph is organized into five lanes. The arrows below mirror the
current Dockerfile `FROM` and artifact-copy relationships.

```text
External bases
  debian:trixie-slim
  python:3.13-slim-trixie

Shared base and artifact stages
  debian:trixie-slim
    -> pek-demo-media
    -> pek-build-base
       -> pek-cross-build-base
  python:3.13-slim-trixie
    -> pek-models

Development tooling lane
  pek-build-base
    -> pek-dev-base
    -> pek-dev-tools
    -> pek-dev
  pek-models
    --copy model artifacts--> pek-dev

Documentation lane
  pek-dev-base
    -> pek-docs

CI lane
  pek-dev-base
    -> pek-ci

Deployment lane
  pek-build-base
    -> pek-cross-build-base
    -> pek-deployment-build
  pek-models
    --copy resolved config and model artifacts--> pek-deployment-build
  pek-demo-media
    --copy demo videos--> pek-deployment-build
  pek-deployment-build
    --copy selected /opt/pek-app and ONNX Runtime outputs-->
  debian:trixie-slim
    -> pek-deployment-base

Helper lane
  python:3.13-slim-trixie
    -> pek-pre-commit-runtime
    -> pek-playwright-pages
```

The main `Dockerfile` owns the cross-lane stage graph where tool stages inherit
from `pek-build-base`, `pek-cross-build-base`, or `pek-dev-base`. The same file
also contains narrow artifact stages for model downloads and demo media so
development and deployment can consume the same resolved inputs without
inheriting artifact-stage tools. Helper runtimes are intentionally separate
top-level Dockerfiles because they do not share the core Debian build graph.

The CI service mapping uses these image lanes without creating new image
contracts for each job. `pek-release-with-ut`, `pek-valgrind-check`,
`pek-generate-valgrind-summary`, `pek-quality-check-full`, `pek-sonar-check`,
`pek-sonar-check-release`, `pek-quality-check-pull-request`, and
`pek-clang-tidy-baseline-check` run in the `pek-ci` image. Repository-check and
report-page jobs use their helper images. Deployment build/audit jobs use
`pek-build-base` and `pek-deployment-base`.

## Motivation

Development and CI containers need broad tooling: compilers, build systems,
formatters, test tools, coverage tools, and diagnostics. Deployment containers
need the opposite: a smaller runtime environment with only the packages and PEK
outputs required to run the application.

Keeping these concerns separate avoids turning the normal development image into
a hidden release artifact and avoids shipping compilers, source trees, temporary
build directories, or quality-check tooling in deployment images.

The intended contract is:

- Tool images provide repeatable environments for building, checking, and
generating artifacts from the current checkout.
- Artifact stages resolve shared inputs such as model files and demo media once,
then copy them into the images that need them.
- Deployment build stages compile and package PEK during image creation.
- Deployment runtime images copy only selected runtime outputs from deployment
build stages.
- Helper images stay narrow and support one workflow, such as pre-commit checks
or report-page publishing.

## Installed Tools By Layer

The lists below name the tools or runtime packages added by each layer. Child
stages inherit everything from their parent unless noted otherwise.

- `pek-build-base`: `ca-certificates`, `curl`, `git`, `build-essential`,
  `meson`, `ninja-build`, `pkg-config`, `cmake`, `unzip`, `python3`, OpenSSL,
  fmt, FFTW, libsoup, JSON-GLib, Cairo, and GStreamer development headers.
- `pek-cross-build-base`: currently inherits `pek-build-base` and gives the
  deployment build lane a named cross-build root.
- `pek-demo-media`: starts from `debian:trixie-slim`, adds `ca-certificates`,
  `curl`, and `bash`, then runs `scripts/download-data.sh` unless
  `NO_EXAMPLE_CONTENT=true`, producing `data/videos` for deployment.
- `pek-models`: starts from `python:3.13-slim-trixie`, adds
  `huggingface_hub==1.18.0`, then runs `scripts/download-models.py` with the
  optional Hugging Face build secret to resolve model artifacts under
  `config/models`.
- `pek-dev-base`: adds `wget`, `sudo`, `gnupg`, `shfmt`, `zip`, `python3-pip`,
  `pre-commit`, `lldb-17`, `valgrind`, `file`, GStreamer runtime plugins,
  `actionlint`, ONNX Runtime, `uv`, the `expkits-ci` tool, `plumber`, and
  `huggingface_hub==1.18.0` in the devtools venv.
- `pek-dev-tools`: adds Executorch packages, locale support, shell/editor tools
  such as `zsh`, Vim, Neovim, Nano, tmux, bash completion, `mc`, debugging and
  language tools such as `gdb` and `clangd`, browser and device tools such as
  Firefox ESR and `v4l-utils`, search/navigation tools such as `ripgrep`,
  `fd-find`, `eza`, and `bat`, Lua/LuaRocks/tree-sitter support, network
  diagnostics such as `iproute2`, `ping`, `traceroute`, `arping`, DNS tools,
  `tcpdump`, and `nmap`, plus VS Code C++ tools and Oh My Zsh setup.
- `pek-dev`: adds optional Raspberry Pi camera packages when `PEK_PICAMERA` is
  enabled, ensures an ARM64 ONNX Runtime path is available, and copies resolved
  model artifacts from `pek-models` into `/opt/pek-app/config/models`. It does
  not copy the repository or prebuilt PEK binaries into the image.
- `pek-docs`: adds `openjdk-25-jdk`, Graphviz, Pandoc, Doxygen, and the
  PlantUML JAR.
- `pek-ci`: adds the docs toolchain plus `gcovr`, Python development and venv
  packages, Python GObject/GStreamer bindings, compression/database development
  libraries, the PlantUML JAR, and Sonar Scanner.
- `pek-deployment-build`: inherits `pek-cross-build-base`, adds the target
  sysroot when cross-building, installs target ONNX Runtime, downloads Meson
  subprojects, consumes resolved model artifacts from `pek-models` and demo
  videos from `pek-demo-media`, builds PEK release outputs, and collects
  `/opt/pek-app`.
- `pek-deployment-base`: contains runtime packages only: OpenSSL, fmt, FFTW,
  libsoup, JSON-GLib, Cairo, GStreamer runtime/tools/plugins, optional Raspberry
  Pi camera runtime packages, ONNX Runtime libraries, and the selected PEK app
  outputs copied from `pek-deployment-build`.
- `pek-pre-commit-runtime`: starts from `python:3.13-slim-trixie` and adds
  `ca-certificates`, `curl`, `git`, `shfmt`, `actionlint`, and `expkits-ci`.
- `pek-playwright-pages`: starts from `python:3.13-slim-trixie` and adds
  `ca-certificates`, GitHub CLI `gh`, and `git`.

## Image Lanes

The development lane starts from shared native build tooling and adds the normal
interactive development environment. The final `pek-dev` image is for working in
a mounted checkout. It may carry resolved model artifacts for first-run setup,
but it should contain tools and dependency libraries, not a prebuilt copy of PEK
from the repository.

The CI lane reuses the development base and adds broad verification tools. CI
jobs build and test the checked-out source at job runtime instead of depending
on PEK binaries baked into the CI image.

The documentation lane is separate from general CI. The `pek-docs` image reuses
the development base and adds documentation tools such as Doxygen, Pandoc,
Graphviz, and PlantUML.

The deployment lane has two roles plus shared artifact inputs.
`pek-deployment-build` inherits the cross-build base, consumes model artifacts
and demo media, compiles PEK, and collects `/opt/pek-app`. `pek-deployment-base`
is the slim runtime image that receives selected outputs from the builder stage.

The helper lane contains small workflow-specific images. `pek-pre-commit-runtime`
runs local repository checks from the host Git hook, and `pek-playwright-pages`
supports report-page publishing.

## Use Cases

Use the development images for interactive work, local builds, debugging, and
running the kit from a mounted checkout.

Use the CI image for release builds, unit tests, coverage, Valgrind, Sonar,
clang-tidy baseline checks, and PR quality gates.

Use the documentation image when generating public docs, Doxygen output, and
PlantUML diagrams.

Use the deployment build and runtime images when producing a runnable deployment
image. This lane combines downloaded model artifacts, demo media, compiled PEK
outputs, and runtime libraries. It is the only lane where PEK binaries should be
built into an image as part of the image creation process.

Use helper images for narrow automation that does not need the full development
or CI environment. The pre-commit runtime is one example: the host hook invokes a
small container that runs repository checks against the mounted checkout.
