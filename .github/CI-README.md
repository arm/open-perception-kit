# Open Perception Kit CI

The workflows in `.github/workflows/` run independently:

- `meson-tests.yml` builds the project and runs the Meson tests.
- `sdk-tests.yml` generates the Perception SDKs and runs Python and Rust tests
  in parallel jobs.
- `valgrind-tests.yml` runs native Meson tests under Valgrind.
- `clang-tidy.yml` analyses changed PR sources, with a full scan for shared
  build/header changes, non-PR runs, or an explicit `all` scope.
- `formatting.yml` checks C/C++, Python, CMake, and shell formatting.
- `hadolint.yml` checks the Dockerfiles.
- `quick-start-smoke.yml` runs quick-start, builds the application, and checks
  browser video playback and inference on x86-64 and AArch64 Linux.
- `lint-pr.yml` validates Conventional Commit PR titles when targeting `main`.

## Shared setup

`.github/actions/setup-sdk` installs the locked SDK toolchain and generates the
language bindings. `.github/actions/setup-build` reuses that action, installs
native build dependencies, and restores the shared compiler cache. Dependency
versions come from `requirements/` and the SDK/runtime descriptors.

There is no separate general-purpose CI image or CI Compose file. Quick-start
smoke uses the existing development image; the native checks run on Ubuntu
runners.

## Local development and pre-commit

The development container remains `opk-dev`, including Valgrind and the
`opk-ci` Python tool. Its pre-commit hooks run that tool inside the container.
Host hooks installed by `scripts/pre-commit/setup.sh` build
`Dockerfile.pre-commit` directly and run the same tool in that separate image.

Removing the legacy CI image does not remove either local checking path.
