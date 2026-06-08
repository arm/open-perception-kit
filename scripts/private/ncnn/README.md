# NCNN Dependency Scripts

These scripts prepare NCNN support for PEK. They are split between the runtime
SDK used by the C++ backend and the Python conversion environment used to create
NCNN `.param` and `.bin` model files.

## Scripts

`setup-ncnn.sh` builds NCNN from source and stages the C/C++ development files
into `/work/deps/ncnn` by default:

- `/work/deps/ncnn/include`
- `/work/deps/ncnn/lib`

The script builds a Release static SDK by default. The source repo, branch, deps
directory, build type, job count, Vulkan support, tools, and shared library mode
can be changed with the options and environment variables listed by `--help`.

`setup-ncnn-env.sh` creates a Python virtual environment for conversion tooling
inside the selected work directory. It installs `pnnx`, `onnx`, `onnxsim`, and
`numpy` by default. It does not copy conversion tools into `/work/deps`; use them
from the created virtual environment.

## Usage

The work directory is mandatory. The scripts fail immediately when it is not
provided.

```sh
scripts/private/ncnn/setup-ncnn.sh /work/var/ncnn-dev
scripts/private/ncnn/setup-ncnn-env.sh /work/var/ncnn-convert
```

After creating the conversion environment:

```sh
# If already in a venv:
deactivate
source /work/var/ncnn-convert/.venv-ncnn/bin/activate
```
