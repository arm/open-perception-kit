#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root="$(cd "${script_dir}/.." && pwd)"

python=python3
venv="${repository_root}/.venv-python-ops"
runtime_descriptor="${repository_root}/development/ops-python/runtime.json"
sdk_descriptor="${repository_root}/tools/perception/sdk.json"
perception_sdk=
architecture=
dry_run=false

usage() {
    cat << 'EOF'
Usage: setup-python-ops-runtime.sh [options]

Options:
  --python PATH                Python interpreter used to create the environment
  --venv PATH                  Destination virtual environment
  --runtime-json PATH          Python Ops runtime dependency descriptor
  --sdk-json PATH              Perception SDK descriptor
  --perception-sdk PATH        Install a local generated Perception Python SDK
  --architecture ARCH          Target architecture: aarch64 or x86_64
  --dry-run                    Validate inputs and print the resolved setup
  -h, --help                   Show this help
EOF
}

while (($#)); do
    case "$1" in
        --python)
            python="${2:?--python requires a path}"
            shift 2
            ;;
        --venv)
            venv="${2:?--venv requires a path}"
            shift 2
            ;;
        --runtime-json)
            runtime_descriptor="${2:?--runtime-json requires a path}"
            shift 2
            ;;
        --sdk-json)
            sdk_descriptor="${2:?--sdk-json requires a path}"
            shift 2
            ;;
        --perception-sdk)
            perception_sdk="${2:?--perception-sdk requires a path}"
            shift 2
            ;;
        --architecture)
            architecture="${2:?--architecture requires a value}"
            shift 2
            ;;
        --dry-run)
            dry_run=true
            shift
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ "$(uname -s)" != Linux ]]; then
    echo "Python Ops runtime setup is supported only on Linux." >&2
    exit 1
fi
if [[ ! -f "${runtime_descriptor}" ]]; then
    echo "Runtime descriptor not found: ${runtime_descriptor}" >&2
    exit 1
fi
if [[ ! -f "${sdk_descriptor}" ]]; then
    echo "SDK descriptor not found: ${sdk_descriptor}" >&2
    exit 1
fi
if [[ -n "${perception_sdk}" && ! -f "${perception_sdk}/pyproject.toml" ]]; then
    echo "Perception Python SDK not found: ${perception_sdk}" >&2
    exit 1
fi
if ! command -v "${python}" > /dev/null 2>&1 && [[ ! -x "${python}" ]]; then
    echo "Python interpreter not found: ${python}" >&2
    exit 1
fi

if [[ -z "${architecture}" ]]; then
    architecture="$(uname -m)"
fi
case "${architecture}" in
    amd64 | x86_64)
        architecture=x86_64
        ;;
    arm64 | aarch64)
        architecture=aarch64
        ;;
    *)
        echo "Unsupported Python Ops runtime architecture: ${architecture}" >&2
        exit 1
        ;;
esac

readarray -t dependency_lock < <("${python}" -c \
    'import json, sys
runtime=json.load(open(sys.argv[1], encoding="utf-8"))["numpy"]
numpy_wheel=runtime["wheels"][sys.argv[3]]
flatbuffers=json.load(open(sys.argv[2], encoding="utf-8"))["flatbuffers"]
flatbuffers_wheel=flatbuffers["python_wheel"]
print(runtime["version"])
print(numpy_wheel["url"])
print(numpy_wheel["sha256"])
print(flatbuffers["version"])
print(flatbuffers_wheel["url"])
print(flatbuffers_wheel["sha256"])' \
    "${runtime_descriptor}" "${sdk_descriptor}" "${architecture}")

if ((${#dependency_lock[@]} != 6)); then
    echo "Invalid Python Ops dependency descriptors." >&2
    exit 1
fi

numpy_version="${dependency_lock[0]}"
numpy_wheel="${dependency_lock[1]}#sha256=${dependency_lock[2]}"
flatbuffers_version="${dependency_lock[3]}"
flatbuffers_wheel="${dependency_lock[4]}#sha256=${dependency_lock[5]}"
venv="$("${python}" -c 'import os, sys; print(os.path.abspath(sys.argv[1]))' "${venv}")"

if [[ "${dry_run}" == true ]]; then
    printf 'python=%s\n' "${python}"
    printf 'venv=%s\n' "${venv}"
    printf 'architecture=%s\n' "${architecture}"
    printf 'numpy=%s\n' "${numpy_version}"
    printf 'flatbuffers=%s\n' "${flatbuffers_version}"
    if [[ -n "${perception_sdk}" ]]; then
        printf 'perception_sdk=%s\n' "${perception_sdk}"
    fi
    exit 0
fi

if [[ -e "${venv}" && ! -x "${venv}/bin/python" ]]; then
    echo "Destination exists but is not a Python virtual environment: ${venv}" >&2
    exit 1
fi
if [[ ! -x "${venv}/bin/python" ]]; then
    "${python}" -m venv "${venv}"
fi

"${venv}/bin/python" -m pip install --no-cache-dir \
    "${numpy_wheel}" \
    "${flatbuffers_wheel}"
if [[ -n "${perception_sdk}" ]]; then
    "${venv}/bin/python" -m pip install --no-cache-dir --no-deps "${perception_sdk}"
fi

"${venv}/bin/python" -c \
    'import flatbuffers, numpy, sys
expected_numpy, expected_flatbuffers = sys.argv[1:]
if numpy.__version__ != expected_numpy:
    raise SystemExit(f"unexpected NumPy version: {numpy.__version__}")
if flatbuffers.__version__ != expected_flatbuffers:
    raise SystemExit(f"unexpected FlatBuffers version: {flatbuffers.__version__}")' \
    "${numpy_version}" "${flatbuffers_version}"
if [[ -n "${perception_sdk}" ]]; then
    "${venv}/bin/python" -c 'import perception'
fi

printf 'Python Ops runtime ready.\n'
printf 'export PEK_PYTHON_RUNTIME_VENV=%q\n' "${venv}"
