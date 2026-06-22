#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

TARGET_DIR="${1:-.}"
VENV_DIR="${TARGET_DIR}/.venv-rknn"
PYTHON_VERSION="3.11"

echo "[INFO] Target directory: ${TARGET_DIR}"
mkdir -p "${TARGET_DIR}"

echo "[INFO] Deactivating any active venv..."
deactivate 2> /dev/null || true
unset VIRTUAL_ENV || true

echo "[INFO] Ensuring uv is installed..."
if ! command -v uv > /dev/null 2>&1; then
    echo "[INFO] Installing uv..."
    curl -LsSf https://astral.sh/uv/install.sh | sh
    export PATH="$HOME/.local/bin:$PATH"
fi

if ! command -v uv > /dev/null 2>&1; then
    echo "[ERROR] uv not found in PATH"
    echo 'Run: export PATH="$HOME/.local/bin:$PATH"'
    exit 1
fi

echo "[INFO] Installing Python ${PYTHON_VERSION} via uv..."
uv python install "${PYTHON_VERSION}"

echo "[INFO] Removing existing venv if present..."
rm -rf "${VENV_DIR}"

echo "[INFO] Creating virtual environment..."
uv venv --python "${PYTHON_VERSION}" --seed "${VENV_DIR}"

echo "[INFO] Activating virtual environment..."
# shellcheck disable=SC1090
source "${VENV_DIR}/bin/activate"

echo "[INFO] Python in venv:"
python --version

echo "[INFO] Fixing RKNN compatibility (setuptools)..."
python -m pip install "setuptools<81"

echo "[INFO] Upgrading packaging tools..."
python -m pip install -U pip wheel

echo "[INFO] Installing RKNN Toolkit2..."
python -m pip install rknn-toolkit2

echo "[INFO] Verifying RKNN installation..."
python - << 'EOF'
from rknn.api import RKNN
rknn = RKNN()
print("RKNN Toolkit2 initialized successfully")
rknn.release()
EOF

echo
echo "[SUCCESS] RKNN environment ready!"
echo
echo "Activate it with:"
echo "  # If already in a venv: deactivate"
echo "  source ${VENV_DIR}/bin/activate"
