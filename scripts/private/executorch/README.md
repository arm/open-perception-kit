# ExecuTorch Dependency Scripts

These scripts prepare ExecuTorch C/C++ development files for PEK. They keep
clone, source, build, virtualenv, download, cache, and temporary state inside the
work directory you pass on the command line, then stage the usable SDK files into
`/work/deps` by default.

## Scripts

`setup-executorch-deps.sh` clones ExecuTorch from git, builds it, and stages:

- `/work/deps/executorch/include`
- `/work/deps/executorch/lib`
- `/work/deps/libtorch/include`

The default ExecuTorch ref is `release/1.0`. You can override the repo, ref,
deps directory, and build parallelism with the script options or environment
variables listed by `--help`.

`setup-executorch-1.3.1-deps.sh` uses the official ExecuTorch `v1.3.1` source
archive instead of cloning the top-level source from a live branch. Git is still
used to recover the pinned submodule commits for that tag, because GitHub source
archives do not contain submodule contents.

`package-executorch-1.3.1-deb.sh` creates a Debian package from an SDK already
staged by the ExecuTorch 1.3.1 setup script, without rebuilding it.

## Usage

The work directory is mandatory. The scripts fail immediately when it is not
provided.

```sh
scripts/private/executorch/setup-executorch-deps.sh /work/var/executorch-build
scripts/private/executorch/setup-executorch-1.3.1-deps.sh /work/var/executorch-1.3.1-build
```

Use a separate work directory per target architecture or ExecuTorch version.
Reusing binaries across incompatible architectures is not supported.

## ExecuTorch 1.3.1 target architectures

`setup-executorch-1.3.1-deps.sh` accepts `--target-arch x86_64` for an x86_64
Linux GNU build and `--target-arch arm` for an ARMv7 Linux EABI build. Both
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

The ARM build uses the Debian/Ubuntu GCC 14 `arm-linux-gnueabi` toolchain and
requires:

```sh
sudo apt install \
  gcc-14-arm-linux-gnueabi \
  g++-14-arm-linux-gnueabi \
  binutils-arm-linux-gnueabi
```

The generated ARM libraries use the ARMv7-A architecture, the Linux EABI
soft-float calling convention, and NEON instructions. Compiler and binutils
commands can be overridden with the `EXECUTORCH_ARM_CC`,
`EXECUTORCH_ARM_CXX`, `EXECUTORCH_ARM_AR`, `EXECUTORCH_ARM_RANLIB`, and
`EXECUTORCH_ARM_STRIP` environment variables.

## Debian package

The ExecuTorch 1.3.1 setup script creates a Debian development package after
staging and validating the SDK. Packages are written to `/work/var` by default
and use the `name-version-revision-arch.deb` layout, for example:

- `libexecutorch-dev-1.3.1-1-amd64.deb`
- `libexecutorch-dev-1.3.1-1-armel.deb`

The `arm-linux-gnueabi` target maps to Debian's `armel` architecture because it
uses the soft-float ABI. Installing the package creates:

- `/work/var/executorch/include`
- `/work/var/executorch/lib`
- `/work/var/libtorch/include`

PEK discovers this installed layout automatically. Install a generated package
with:

```sh
sudo apt install /work/var/libexecutorch-dev-1.3.1-1-amd64.deb
```

Automatic detection only uses the installed `/work/var/executorch` SDK. To
build directly from the staging tree without installing the package, select both
staged roots explicitly:

```sh
PEK_EXECUTORCH_ROOT=/work/deps/executorch \
PEK_LIBTORCH_ROOT=/work/deps/libtorch \
PEK_EXECUTORCH=enabled ./scripts/build-elements.sh debug
```

Use `--deb-output-dir` and `--deb-revision` to change the artifact directory or
package revision. Use `--skip-deb` when only the staged SDK is needed.

An already-staged SDK can be packaged again without rebuilding ExecuTorch:

```sh
scripts/private/executorch/package-executorch-1.3.1-deb.sh \
  --executorch-dir /work/deps/executorch \
  --libtorch-dir /work/deps/libtorch \
  --output-dir /work/var \
  --revision 1
```
