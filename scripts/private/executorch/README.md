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

## Usage

The work directory is mandatory. The scripts fail immediately when it is not
provided.

```sh
scripts/private/executorch/setup-executorch-deps.sh /work/var/executorch-build
scripts/private/executorch/setup-executorch-1.3.1-deps.sh /work/var/executorch-1.3.1-build
```

Use a separate work directory per target architecture or ExecuTorch version.
Reusing binaries across incompatible architectures is not supported.
