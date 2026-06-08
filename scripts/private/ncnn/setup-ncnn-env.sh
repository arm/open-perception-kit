#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

WORK_DIR=""
PYTHON_VERSION="${PYTHON_VERSION:-3.11}"
NCNN_CONVERSION_PACKAGES="${NCNN_CONVERSION_PACKAGES:-pnnx onnx onnxsim numpy}"

usage() {
    cat <<'EOF'
Create an NCNN model-conversion environment for PEK.

Usage:
  scripts/private/ncnn/setup-ncnn-env.sh <work-dir> [options]

Example:
  scripts/private/ncnn/setup-ncnn-env.sh /work/var/ncnn-convert

Options:
  --work-dir DIR    Directory used for the Python venv.
  --help            Show this help.

Environment:
  PYTHON_VERSION            Python major/minor version. Default: 3.11.
  NCNN_CONVERSION_PACKAGES  Pip packages to install. Default: pnnx onnx onnxsim numpy.

Output:
  $WORK_DIR/.venv-ncnn
EOF
}

die() {
    echo "[ERROR] $*" >&2
    exit 1
}

resolve_path() {
    local path="$1"
    local parent
    local base

    case "${path}" in
        /*) ;;
        *) path="${ORIGINAL_CWD}/${path}" ;;
    esac

    parent="$(dirname -- "${path}")"
    base="$(basename -- "${path}")"
    mkdir -p "${parent}"
    parent="$(cd -- "${parent}" && pwd -P)"
    printf '%s/%s\n' "${parent}" "${base}"
}

need_cmd() {
    if ! command -v "$1" > /dev/null 2>&1; then
        die "Required command not found: $1"
    fi
}

ORIGINAL_CWD="$(pwd -P)"
WORK_DIR_ARG_PROVIDED=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --work-dir)
            [[ $# -ge 2 ]] || die "--work-dir requires a value"
            [[ "${WORK_DIR_ARG_PROVIDED}" -eq 0 ]] || die "work-dir specified more than once"
            WORK_DIR="$2"
            WORK_DIR_ARG_PROVIDED=1
            shift 2
            ;;
        --help | -h)
            usage
            exit 0
            ;;
        --*)
            die "Unknown option: $1"
            ;;
        *)
            [[ "${WORK_DIR_ARG_PROVIDED}" -eq 0 ]] || die "work-dir specified more than once"
            WORK_DIR="$1"
            WORK_DIR_ARG_PROVIDED=1
            shift
            ;;
    esac
done

if [[ "${WORK_DIR_ARG_PROVIDED}" -eq 0 || -z "${WORK_DIR}" ]]; then
    usage >&2
    die "work-dir argument is mandatory"
fi

WORK_DIR="$(resolve_path "${WORK_DIR}")"
[[ "${WORK_DIR}" != "/" ]] || die "Refusing to use / as work directory"

VENV_DIR="${VENV_DIR:-${WORK_DIR}/.venv-ncnn}"

echo "[INFO] Work dir: ${WORK_DIR}"
echo "[INFO] Python version: ${PYTHON_VERSION}"

need_cmd curl
need_cmd mkdir
need_cmd rm

echo "[INFO] Ensuring uv is installed..."
if ! command -v uv > /dev/null 2>&1; then
    curl -LsSf https://astral.sh/uv/install.sh | sh
    export PATH="${HOME}/.local/bin:${PATH}"
fi

if ! command -v uv > /dev/null 2>&1; then
    die 'uv not found in PATH; run: export PATH="$HOME/.local/bin:$PATH"'
fi

echo "[INFO] Installing Python ${PYTHON_VERSION} via uv..."
uv python install "${PYTHON_VERSION}"

echo "[INFO] Recreating NCNN conversion venv..."
rm -rf "${VENV_DIR}"
mkdir -p "${WORK_DIR}"
uv venv --python "${PYTHON_VERSION}" --seed "${VENV_DIR}"

# shellcheck disable=SC1090
source "${VENV_DIR}/bin/activate"

python - "${PYTHON_VERSION}" <<'PY'
import sys
expected = tuple(int(part) for part in sys.argv[1].split(".")[:2])
if sys.version_info[:2] != expected:
    raise SystemExit(
        f"NCNN conversion env requires Python {sys.argv[1]}, got {sys.version.split()[0]}"
    )
PY

echo "[INFO] Upgrading packaging tools..."
python -m pip install -U pip wheel setuptools

echo "[INFO] Installing NCNN conversion packages: ${NCNN_CONVERSION_PACKAGES}"
# shellcheck disable=SC2086
python -m pip install ${NCNN_CONVERSION_PACKAGES}

if [[ ! -x "${VENV_DIR}/bin/pnnx" ]]; then
    die "pnnx command was not installed into ${VENV_DIR}/bin"
fi

python - <<'PY'
import importlib
for name in ("pnnx", "onnx", "numpy"):
    importlib.import_module(name)
print("NCNN conversion Python packages imported successfully")
PY

echo
echo "[SUCCESS] NCNN conversion environment ready"
echo "  venv -> ${VENV_DIR}"
echo
echo "Activate it with:"
echo "  # If already in a venv: deactivate"
echo "  source ${VENV_DIR}/bin/activate"
