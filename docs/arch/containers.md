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
  python:3.12-slim-trixie
  python:3.13-slim-trixie

Development tooling lane
  debian:trixie-slim
    -> pek-build-base
    -> pek-dev-base
    -> pek-dev-tools
    -> pek-dev

Documentation lane
  pek-dev-base
    -> pek-docs

CI lane
  pek-dev-base
    -> pek-ci

Deployment lane
  pek-build-base
    -> pek-deployment-build
       --copy selected /opt/pek-app and ONNX Runtime outputs-->
  debian:trixie-slim
    -> pek-deployment-base

Helper lane
  python:3.12-slim-trixie
    -> pek-pre-commit-runtime

  python:3.13-slim-trixie
    -> pek-playwright-pages
```

The main `Dockerfile` owns the cross-lane stage graph where stages inherit from
`pek-build-base` or `pek-dev-base`. Helper runtimes are intentionally separate
top-level Dockerfiles because they start from Python images and do not share the
core Debian build graph.

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
- `pek-dev-base`: adds `wget`, `sudo`, `gnupg`, `shfmt`, `zip`, `python3-pip`,
  `pre-commit`, `lldb-17`, `valgrind`, `file`, GStreamer runtime plugins,
  `actionlint`, ONNX Runtime, `uv`, the `expkits-ci` tool, and `plumber`.
- `pek-dev-tools`: adds Executorch packages, locale support, shell/editor tools
  such as `zsh`, Vim, Neovim, Nano, tmux, bash completion, `mc`, debugging and
  language tools such as `gdb` and `clangd`, browser and device tools such as
  Firefox ESR and `v4l-utils`, search/navigation tools such as `ripgrep`,
  `fd-find`, `eza`, and `bat`, Lua/LuaRocks/tree-sitter support, network
  diagnostics such as `iproute2`, `ping`, `traceroute`, `arping`, DNS tools,
  `tcpdump`, and `nmap`, plus VS Code C++ tools and Oh My Zsh setup.
- `pek-dev`: adds optional Raspberry Pi camera packages when `PEK_PICAMERA` is
  enabled and ensures an ARM64 ONNX Runtime path is available. It does not copy
  the repository or prebuilt PEK outputs into the image.
- `pek-docs`: adds `openjdk-25-jdk`, Graphviz, Pandoc, Doxygen, and the
  PlantUML JAR.
- `pek-ci`: adds the docs toolchain plus `gcovr`, Python development and venv
  packages, Python GObject/GStreamer bindings, compression/database development
  libraries, the PlantUML JAR, and Sonar Scanner.
- `pek-deployment-build`: adds the target sysroot when cross-building, installs
  target ONNX Runtime, downloads Meson subprojects, builds PEK release outputs,
  and collects `/opt/pek-app`.
- `pek-deployment-base`: contains runtime packages only: OpenSSL, fmt, FFTW,
  libsoup, JSON-GLib, Cairo, GStreamer runtime/tools/plugins, optional Raspberry
  Pi camera runtime packages, ONNX Runtime libraries, and the selected PEK app
  outputs copied from `pek-deployment-build`.
- `pek-pre-commit-runtime`: starts from `python:3.12-slim-trixie` and adds
  `ca-certificates`, `curl`, `git`, `shfmt`, `actionlint`, and `expkits-ci`.
- `pek-playwright-pages`: starts from `python:3.13-slim-trixie` and adds
  `ca-certificates`, GitHub CLI `gh`, and `git`.

## Image Lanes

The development lane starts from shared native build tooling and adds the normal
interactive development environment. The final `pek-dev` image is for working in
a mounted checkout. It should contain tools and dependency libraries, not a
prebuilt copy of PEK from the repository.

The CI lane reuses the development base and adds broad verification tools. CI
jobs build and test the checked-out source at job runtime instead of depending
on PEK binaries baked into the CI image.

The documentation lane is separate from general CI. The `pek-docs` image reuses
the development base and adds documentation tools such as Doxygen, Pandoc,
Graphviz, and PlantUML.

The deployment lane has two roles. `pek-deployment-build` is the builder stage
that compiles PEK and collects `/opt/pek-app`. `pek-deployment-base` is the slim
runtime image that receives selected outputs from the builder stage.

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
image. This is the only lane where PEK binaries should be built into an image as
part of the image creation process.

Use helper images for narrow automation that does not need the full development
or CI environment. The pre-commit runtime is one example: the host hook invokes a
small container that runs repository checks against the mounted checkout.
