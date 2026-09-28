---
title: Build the Debian runtime package
sidebar_label: Debian runtime package
description: Build the architecture-specific OPK GStreamer runtime package through Meson.
---

# Build the Debian runtime package

The `opk-runtime` Debian package contains the six OPK GStreamer plugins, their
private common and operation modules, ONNX Runtime, the statically linked
ExecuTorch operation module, the PythonScript operation with its locked private
NumPy, FlatBuffers, and Open Perception Kit packages, and the matching SDK
release triplet. It also contains the browser assets required by `opksink` under
`/usr/share/opk/web` and the canonical Perception FlatBuffers schemas under
`/usr/share/opk/schemas/flatbuffers`. The complete checked-in documentation tree
is available under `/usr/share/opk/docs`.
It deliberately excludes models, model descriptors, OpChains, pipelines,
sample media, and the OPK launcher. Those architecture-independent runtime
resources belong in a separate data package.

The package targets Debian 13 (Trixie) on `amd64` and `arm64`. Meson owns the
package target so the native libraries are built before assembly and staged
with their install RUNPATHs.

## Prepare ExecuTorch

The quick-start container can run without ExecuTorch, but `opk-runtime`
requires a complete ExecuTorch SDK so it can statically link
`opk-executorch-ops.so`. Build and stage the pinned ExecuTorch 1.3.1 SDK from
the repository root:

```bash
./scripts/private/executorch/setup-executorch-1.3.1-deps.sh \
  "$PWD/var/executorch-1.3.1-build"
```

This source build can take a significant amount of time. Its outputs used by
the OPK build are:

```text
deps/executorch/
deps/libtorch/
deps/executorch-legal-documentation/
```

The setup script creates an isolated Python 3.11 build environment with `uv`;
the shell's active Python and the container's `PYTHON_VERSION` value do not
select the ExecuTorch build interpreter. If a failed older run left a non-3.11
environment in the work directory, restart once with `--clean-work-dir`.

Verify the staged SDK:

```bash
test -f "$PWD/deps/executorch/include/executorch/extension/module/module.h"
test -f "$PWD/deps/executorch/lib/libexecutorch.a"
test -d "$PWD/deps/libtorch/include"
test -d "$PWD/deps/executorch-legal-documentation"
```

The resulting `opk-runtime` package contains the statically linked OPK
ExecuTorch operation module, not a separate ExecuTorch runtime library. Its
required license and third-party notices must still be packaged because the
code is incorporated into that module. Package assembly installs the staged
legal tree, tag commit, and version under:

```text
/usr/share/opk/licenses/executorch/LICENSE
/usr/share/opk/licenses/executorch/GIT_COMMIT_ID
/usr/share/opk/licenses/executorch/VERSION_NUMBER
/usr/share/opk/licenses/executorch/third-party/
```

## Prepare the Open Perception Kit SDK

Create the Open Perception Kit SDK triplet from the same clean commit that will build
the Debian package:

```bash
mkdir -p "$PWD/artifacts/open-perception-kit"

./scripts/perception-sdk.sh package \
  --output-dir "$PWD/artifacts/open-perception-kit" \
  --expect-version "$(sed -n "s/^[[:space:]]*version: '\([^']*\)'.*/\1/p" development/meson.build)" \
  --artifact-dir /opt/opk-deps/open-perception-kit-artifacts
```

The directory must contain exactly the versioned SDK ZIP, its SHA-256 sidecar,
and its provenance sidecar. Package assembly verifies the triplet and rejects
dirty provenance or a repository commit mismatch.

## Verify the Python Ops runtime

The supported development container installs the generated Open Perception Kit Python
package, NumPy, and FlatBuffers into the private Python Ops environment
automatically. Confirm that the environment is complete before configuring
Meson:

```bash
/opt/opk-venvs/python-ops-runtime/bin/python -c \
  'import importlib.metadata, flatbuffers, numpy, open_perception_kit; print(importlib.metadata.version("open-perception-kit"))'
```

The printed SDK version must match the OPK version. If the command reports that
`open-perception-kit` is missing from an existing container, repair that private
environment with:

```bash
sudo /opt/opk-venvs/python-ops-runtime/bin/python -m pip install \
  --no-cache-dir \
  --no-deps \
  /work/generated/open_perception_kit/python
```

This command is a recovery step for an existing or stale container. Rebuild the
development container from the current Dockerfile to make the installation part
of the image. Sourcing either virtual environment is neither required nor
sufficient: Meson records the explicit interpreter selected through
`OPK_PYTHON_RUNTIME_VENV`.

## Configure the package build

Configure a release build inside the supported OPK development container. The
ExecuTorch SDK and ONNX Runtime must be available through their normal OPK
dependency locations or environment overrides:

```bash
rm -rf "$PWD/development/build-deb"

OPK_ONNXRUNTIME_ROOT=/opt/opk-deps/onnxruntime \
OPK_EXECUTORCH_ROOT="$PWD/deps/executorch" \
OPK_LIBTORCH_ROOT="$PWD/deps/libtorch" \
OPK_PYTHON_RUNTIME_VENV=/opt/opk-venvs/python-ops-runtime \
meson setup "$PWD/development/build-deb" "$PWD/development" \
  --buildtype=release \
  -Drelease_package=true \
  -Ddeb_package=true \
  -Ddeb_package_revision=1 \
  -Dopen_perception_kit_dir="$PWD/artifacts/open-perception-kit" \
  -Dexecutorch=enabled \
  -Dpython_ops=enabled
```

`open_perception_kit_dir` must be an absolute path. The package revision is appended
to the OPK product version using Debian's `<upstream>-<revision>` format. Python
Ops uses Debian Trixie's Python 3.13 runtime and the locked environment selected
through `OPK_PYTHON_RUNTIME_VENV` when Meson is configured.

## Build the package

Run the Meson package target:

```bash
meson compile -C development/build-deb opk-runtime-deb
```

The build directory receives one architecture-specific package, for example:

```text
development/build-deb/packaging/opk-runtime_0.3.1-1_amd64.deb
```

The assembler derives shared-library dependencies with `dpkg-shlibdeps` and
adds the GStreamer Base, Good, Bad, and Nice runtime plugin packages explicitly.
It validates the ELF architecture, private-library and plugin RUNPATHs, ONNX
Runtime SONAME link, SDK provenance, and the allowlisted package contents before
calling `dpkg-deb`.

Inspect the result without installing it:

```bash
dpkg-deb --info development/build-deb/packaging/opk-runtime_*.deb
dpkg-deb --contents development/build-deb/packaging/opk-runtime_*.deb
```
