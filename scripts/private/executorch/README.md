# ExecuTorch Dependency Scripts

These scripts prepare ExecuTorch C/C++ development files for PEK. They keep
clone, source, build, virtualenv, download, cache, and temporary state inside the
work directory you pass on the command line, then stage the usable SDK files into
`$PEK_PROJECT_ROOT/deps` by default. `PEK_PROJECT_ROOT` defaults to the checkout
containing the scripts when it is not set explicitly.

## Scripts

`setup-executorch-1.3.1-deps.sh` uses the official ExecuTorch `v1.3.1` source
archive instead of cloning the top-level source from a live branch. Git is still
used to recover the pinned submodule commits for that tag, because GitHub source
archives do not contain submodule contents. It stages:

- `$PEK_PROJECT_ROOT/deps/executorch/include`
- `$PEK_PROJECT_ROOT/deps/executorch/lib`
- `$PEK_PROJECT_ROOT/deps/libtorch/include`
- `$PEK_PROJECT_ROOT/deps/executorch-legal-documentation` for ExecuTorch and
  third-party licenses/copyright notices

`package-executorch-1.3.1-deb.sh` creates a Debian package from an SDK already
staged by the ExecuTorch 1.3.1 setup script, without rebuilding it.

`install-executorch-deb.sh` is the Docker build helper that installs an
architecture-matching local package when available, otherwise tries the
configured Artifactory Debian repository, and otherwise continues without
ExecuTorch support.

`upload-executorch-1.3.1-deb.sh` uploads a generated package to the PEK Debian
repository in Artifactory and recalculates its Debian repository metadata.

## Usage

Run these commands from the repository root. The work directory is mandatory;
the setup script fails immediately when it is not provided. If the development
environment has not already set the project root, set it for the current shell:

```sh
export PEK_PROJECT_ROOT="$PWD"
scripts/private/executorch/setup-executorch-1.3.1-deps.sh \
  "$PEK_PROJECT_ROOT/var/executorch-1.3.1-build"
```

The ExecuTorch 1.3.1 setup reuses its work directory and CMake build directory
by default. This preserves the source checkout, Python virtual environment,
downloads, package caches, and incremental build state. Use `--clean-build` to
recreate only the CMake build directory, or `--clean-work-dir` for a completely
pristine build. The latter removes all cached state below the selected work
directory before rebuilding.

The setup also records a successful ExecuTorch Python installation in the
virtual environment. Later runs skip the upstream `install_executorch.sh` when
the Python interpreter, installer inputs, dependency manifests, and relevant
pinned submodule revisions are unchanged and the installed packages still
import successfully. No files in the downloaded ExecuTorch source are modified
to provide this cache.

Build parallelism defaults to the smaller of the available CPU capacity and
one job per 2 GiB of available memory. The calculation accounts for Linux
cgroup v2 CPU and memory limits when present. Set `JOBS=N` or pass `--jobs N` to
override it; the command-line option takes precedence over the environment.

When `ccache` is installed, the setup automatically uses it as the C and C++
compiler launcher. Its default cache directory is `$WORK_DIR/cache/ccache`, so
`--clean-build` retains cached compiler results while `--clean-work-dir` removes
them. Set the standard `CCACHE_DIR` environment variable to use a shared or
external cache instead. Builds continue normally when `ccache` is unavailable.

Use a separate work directory per target architecture or ExecuTorch version.
Reusing binaries across incompatible architectures is not supported.

## ExecuTorch 1.3.1 target architectures

`setup-executorch-1.3.1-deps.sh` accepts `--target-arch x86_64` for an x86_64
Linux GNU build and `--target-arch arm` for an AArch64 Linux GNU build. Both
targets use explicit toolchain files, so the selected target does not depend on
the build machine architecture:

```sh
scripts/private/executorch/setup-executorch-1.3.1-deps.sh \
  "$PEK_PROJECT_ROOT/var/executorch-1.3.1-x86_64-build" \
  --target-arch x86_64

scripts/private/executorch/setup-executorch-1.3.1-deps.sh \
  "$PEK_PROJECT_ROOT/var/executorch-1.3.1-arm-build" \
  --target-arch arm
```

The x86_64 build uses the Debian/Ubuntu GCC 14 `x86_64-linux-gnu` toolchain. On
an Arm build machine, including an Arm Linux container running on an Apple
silicon host, install:

```sh
sudo apt install \
  ccache \
  gcc-14-x86-64-linux-gnu \
  g++-14-x86-64-linux-gnu \
  binutils-x86-64-linux-gnu
```

Its commands can be overridden with `EXECUTORCH_X86_64_CC`,
`EXECUTORCH_X86_64_CXX`, `EXECUTORCH_X86_64_AR`,
`EXECUTORCH_X86_64_RANLIB`, and `EXECUTORCH_X86_64_STRIP`.

The Arm build uses the Debian/Ubuntu GCC 14 `aarch64-linux-gnu` toolchain and
requires:

```sh
sudo apt install \
  ccache \
  gcc-14-aarch64-linux-gnu \
  g++-14-aarch64-linux-gnu \
  binutils-aarch64-linux-gnu
```

The generated Arm libraries use the 64-bit Armv8-A architecture and the
AArch64 Linux GNU ABI. Compiler and binutils commands can be overridden with
the `EXECUTORCH_ARM_CC`,
`EXECUTORCH_ARM_CXX`, `EXECUTORCH_ARM_AR`, `EXECUTORCH_ARM_RANLIB`, and
`EXECUTORCH_ARM_STRIP` environment variables.

## Debian package

The ExecuTorch 1.3.1 setup script creates a Debian development package after
staging and validating the SDK. Packages are written to
`$PEK_PROJECT_ROOT/var` by default and use the
`name-version-revision-arch.deb` layout, for example:

- `libexecutorch-dev-1.3.1-2-amd64.deb`
- `libexecutorch-dev-1.3.1-2-arm64.deb`

The `aarch64-linux-gnu` target maps to Debian's `arm64` architecture. Installing
the package creates:

- `/opt/pek-deps/executorch/include`
- `/opt/pek-deps/executorch/lib`
- `/opt/pek-deps/libtorch/include`
- `/opt/pek-deps/executorch-legal-documentation`

PEK discovers this installed layout automatically. Install a generated package
with:

```sh
sudo apt install "$PEK_PROJECT_ROOT/var/libexecutorch-dev-1.3.1-2-amd64.deb"
```

Automatic detection only uses the installed `/opt/pek-deps/executorch` SDK. To
build directly from the staging tree without installing the package, select both
staged roots explicitly:

```sh
PEK_EXECUTORCH_ROOT="$PEK_PROJECT_ROOT/deps/executorch" \
PEK_LIBTORCH_ROOT="$PEK_PROJECT_ROOT/deps/libtorch" \
PEK_EXECUTORCH=enabled ./scripts/build-elements.sh debug
```

Use `--deb-output-dir` and `--deb-revision` to change the artifact directory or
package revision. Use `--skip-deb` when only the staged SDK is needed.

An already-staged SDK can be packaged again without rebuilding ExecuTorch:

```sh
scripts/private/executorch/package-executorch-1.3.1-deb.sh \
  --executorch-dir "$PEK_PROJECT_ROOT/deps/executorch" \
  --libtorch-dir "$PEK_PROJECT_ROOT/deps/libtorch" \
  --legal-documentation-dir "$PEK_PROJECT_ROOT/deps/executorch-legal-documentation" \
  --output-dir "$PEK_PROJECT_ROOT/var" \
  --revision 2
```

## Docker image installation

PEK development image builds first look for an architecture-matching package
in the repository's `var` directory. If the local package is not present or is
invalid, the build tries the configured Artifactory Debian repository. If both
sources are unavailable, the image is built without ExecuTorch support.

The Dockerfile accepts these build arguments:

| Argument | Default | Purpose |
| --- | --- | --- |
| `EXECUTORCH_VERSION` | `1.3.1` | Package version to install. |
| `EXECUTORCH_DEB_REVISION` | `2` | Debian package revision to install. |
| `EXECUTORCH_ARTIFACTORY_SERVER` | `https://artifactory.arm.com:443` | Artifactory server URL. |
| `EXECUTORCH_ARTIFACTORY_REPOSITORY` | `ai-expkits-internal.opk-deb` | Artifactory Debian repository. |
| `EXECUTORCH_ARTIFACTORY_DISTRIBUTION` | `trixie` | Debian distribution. |
| `EXECUTORCH_ARTIFACTORY_COMPONENT` | `main` | Debian component. |
| `EXECUTORCH_ARTIFACTORY_USERNAME` | Empty | Artifactory username. |
| `EXECUTORCH_ARTIFACTORY_PASSWORD` | Empty | Artifactory access token. |

The Artifactory fallback requires both credential arguments. Store them in the
ignored repository-root `.env` file rather than committing them:

```dotenv
EXECUTORCH_ARTIFACTORY_USERNAME='<username>'
EXECUTORCH_ARTIFACTORY_PASSWORD='<access-token>'
```

`EXECUTORCH_ARTIFACTORY_PASSWORD` is intended to contain an access token, not a
long-lived account password. The credentials are passed as Docker build
arguments and may be visible in build metadata or caches, so use a suitably
scoped token and do not share the resulting build metadata.

Both container launch paths pass the root `.env` file to Docker Compose when it
exists:

```sh
./scripts/private/run-console.sh up
./scripts/quick_start.sh
```

### Build network selection

`BUILD_NETWORK_MODE` controls the network available to Dockerfile `RUN`
instructions. It is a Docker Compose interpolation variable used by
`build.network`, not a Dockerfile build argument. BuildKit calls its normal
bridge/NAT-backed build network `default`.

Use the default network when the host VPN is disconnected:

```dotenv
BUILD_NETWORK_MODE=default
```

When the Artifactory repository is only reachable through the Arm VPN, select
host networking in the root `.env` file:

```dotenv
BUILD_NETWORK_MODE=host
```

The setting can also be overridden for one invocation. A shell variable takes
precedence over the value in `.env`:

```sh
BUILD_NETWORK_MODE=host ./scripts/private/run-console.sh up
BUILD_NETWORK_MODE=host ./scripts/quick_start.sh
```

This setting affects the image build only. The separately configured Compose
`network_mode` controls networking after the container starts.

## Upload a Debian package

Set the Artifactory username and a newly generated access token in the
environment, then pass the package produced by the packaging script:

```sh
export EXECUTORCH_ARTIFACTORY_USERNAME='<username>'
export EXECUTORCH_ARTIFACTORY_PASSWORD='<access-token>'

scripts/private/executorch/upload-executorch-1.3.1-deb.sh \
  "$PEK_PROJECT_ROOT/var/libexecutorch-dev-1.3.1-2-amd64.deb"
```

The defaults upload to the `ai-expkits-internal.opk-deb` repository under the
`trixie/main` coordinates. The package architecture is read from the Debian
control metadata. Use `--help` to see repository overrides and the option to
skip metadata recalculation.
