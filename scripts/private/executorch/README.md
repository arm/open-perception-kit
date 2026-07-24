# ExecuTorch Dependency Scripts

These scripts prepare ExecuTorch C/C++ development files for PEK. They keep
clone, source, build, virtualenv, download, cache, and temporary state inside the
work directory you pass on the command line, then stage the usable SDK files into
`/work/deps` by default.

## Scripts

`setup-executorch-1.3.1-deps.sh` uses the official ExecuTorch `v1.3.1` source
archive instead of cloning the top-level source from a live branch. Git is still
used to recover the pinned submodule commits for that tag, because GitHub source
archives do not contain submodule contents. It populates only the core and
XNNPACK submodules selected by the PEK SDK build and installs only its required
Python build dependencies: `torch==2.12.0+cpu` for `torchgen`,
`PyYAML==6.0.1`, and `typing_extensions==4.13.2`. Their architecture-specific
wheel URLs and SHA-256 values are checked in; the installer does not resolve an
index or install transitive packages. The wheel marker follows the build host,
including during cross-compilation, because the host runs the code generator.

The script verifies the source archive against its checked-in SHA-256 and
requires the `v1.3.1` tag to resolve to its checked-in commit before using the
tag's submodule gitlinks.

`package-executorch-1.3.1-deb.sh` creates a Debian package from an SDK already
staged by the ExecuTorch 1.3.1 setup script, without rebuilding it. It normalizes
package timestamps to the pinned v1.3.1 tag commit so identical staged SDK input
produces byte-identical package output.

`install-executorch-deb.sh` is the Docker build helper used in two isolated
steps. The first selects an architecture-matching local package or downloads it
from the configured Artifactory Debian repository. The second installs that
staged package after the credential mounts are no longer present.

An Artifactory download is accepted only through the checked-in Debian signing
key and its pinned fingerprint. The helper verifies `Release.gpg`, then the
signed SHA-256 and size of `Packages`, then the package SHA-256, size, and Debian
control metadata. Authenticity or metadata failures always stop the build,
including when ExecuTorch itself is optional.

`upload-executorch-1.3.1-deb.sh` uploads a generated package to the PEK Debian
repository in Artifactory. Publication must use an Artifactory identity with
Deploy/Cache permission and without Delete/Overwrite permission. That
server-side permission is the write-once authority; the script's checksum
preflight only provides idempotent retry and a clearer error. Artifactory
automatically updates Debian metadata after the matrix-parameter upload. The
script completes only after the exact package is visible through the signed
`Release` → `Packages` → package chain, while different bytes require a new
Debian revision. Before upload, it also rejects maintainer scripts and payload
outside the exact header/static-library SDK shape produced by the package
script.

## Usage

The work directory is mandatory. The scripts fail immediately when it is not
provided.

```sh
scripts/private/executorch/setup-executorch-1.3.1-deps.sh /work/var/executorch-1.3.1-build
```

Use a separate work directory per target architecture or ExecuTorch version.
Reusing binaries across incompatible architectures is not supported.

## ExecuTorch 1.3.1 target architectures

`setup-executorch-1.3.1-deps.sh` accepts `--target-arch x86_64` for an x86_64
Linux GNU build and `--target-arch arm` for an AArch64 Linux GNU build. Both
targets use explicit toolchain files, so the selected target does not depend on
the build machine architecture:

```sh
scripts/private/executorch/setup-executorch-1.3.1-deps.sh \
  /work/var/executorch-1.3.1-x86_64-build \
  --target-arch x86_64

scripts/private/executorch/setup-executorch-1.3.1-deps.sh \
  /work/var/executorch-1.3.1-arm-build \
  --target-arch arm
```

The x86_64 build uses the Debian/Ubuntu GCC 14 `x86_64-linux-gnu` toolchain. On
an Arm build machine, including an Arm Linux container running on an Apple
silicon host, install:

```sh
sudo apt install \
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
staging and validating the SDK. Packages are written to `/work/var` by default
and use the `name-version-revision-arch.deb` layout, for example:

- `libexecutorch-dev-1.3.1-2-amd64.deb`
- `libexecutorch-dev-1.3.1-2-arm64.deb`

The `aarch64-linux-gnu` target maps to Debian's `arm64` architecture. Installing
the package creates:

- `/opt/pek-deps/executorch/include`
- `/opt/pek-deps/executorch/lib`

PEK discovers this installed layout automatically. Install a generated package
with:

```sh
sudo apt install /work/var/libexecutorch-dev-1.3.1-2-amd64.deb
```

Automatic detection only uses the installed `/opt/pek-deps/executorch` SDK. To
build directly from the staging tree without installing the package, select the
staged SDK and enable the backend explicitly:

```sh
PEK_EXECUTORCH_ROOT=/work/deps/executorch \
PEK_EXECUTORCH=enabled ./scripts/build-elements.sh debug
```

Use `--deb-output-dir` and `--deb-revision` to change the artifact directory or
package revision. Use `--skip-deb` when only the staged SDK is needed.

An already-staged SDK can be packaged again without rebuilding ExecuTorch:

```sh
scripts/private/executorch/package-executorch-1.3.1-deb.sh \
  --executorch-dir /work/deps/executorch \
  --output-dir /work/var \
  --revision 2
```

## Docker image installation

PEK development image builds first look for an architecture-matching package
in the repository's `var` directory. If the local package is not present or is
invalid, the build tries the configured Artifactory Debian repository. If both
sources are unavailable, the image is built without ExecuTorch support.
The local package is an explicit operator-provided override. Artifactory
packages instead follow the signed repository metadata chain described above.

The Dockerfile accepts these build arguments:

| Argument | Default | Purpose |
| --- | --- | --- |
| `EXECUTORCH_DEB_REVISION` | `2` | Debian package revision to install. |
| `EXECUTORCH_REQUIRED` | `0` | Use `1` to fail the image build unless a complete SDK is installed. |
| `EXECUTORCH_ARTIFACTORY_SERVER` | `https://artifactory.arm.com:443` | Artifactory server URL. |
| `EXECUTORCH_ARTIFACTORY_REPOSITORY` | `ai-expkits-internal.opk-deb` | Artifactory Debian repository. |
| `EXECUTORCH_ARTIFACTORY_DISTRIBUTION` | `trixie` | Debian distribution. |
| `EXECUTORCH_ARTIFACTORY_COMPONENT` | `main` | Debian component. |

The Artifactory fallback requires both credentials. Store them in the ignored
repository-root `.env` file rather than committing them:

```dotenv
EXECUTORCH_ARTIFACTORY_USERNAME='<username>'
EXECUTORCH_ARTIFACTORY_PASSWORD='<access-token>'
EXECUTORCH_REQUIRED=1
```

`EXECUTORCH_ARTIFACTORY_PASSWORD` is intended to contain an access token, not a
long-lived account password. Docker Compose exposes both values to the
package-fetch step as BuildKit secrets; they are not stored in image metadata
or layers and are no longer mounted when the package is installed.

Required and optional builds use distinct Docker cache keys. Set
`EXECUTORCH_REQUIRED=1` whenever local development uses Artifactory
credentials, so adding credentials cannot reuse an earlier optional
no-package layer. Deployment and trusted release validation set it
automatically; ordinary pull-request image builds keep the optional default
and do not receive Artifactory credentials.

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
  /work/var/libexecutorch-dev-1.3.1-2-amd64.deb
```

The defaults upload to the `ai-expkits-internal.opk-deb` repository under the
`trixie/main` coordinates. The package architecture is read from the Debian
control metadata. Use `--help` to see repository overrides. Increment
`--revision` whenever a package's contents change. Before publishing, verify
that the uploader identity has no Delete/Overwrite permission for the
repository; a client-side existence check cannot replace that atomic
server-side protection.
