#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

# ---------- helpers ----------
log() { echo -e "[setup.sh] $*"; }
die() {
        echo -e "[setup.sh] ERROR: $*" >&2
                                            exit 1
}

append_once() {
    local needle="$1"
    local line="$2"
    grep -Fqx "$needle" "$BASHRC" 2> /dev/null || echo "$line" >> "$BASHRC"
}

trap 'die "failed at line $LINENO"' ERR

# ---------- config ----------
PROJECT_DIR="/work/tools/lazer"
VENV_DIR="$PROJECT_DIR/.venv"
BASHRC="$HOME/.bashrc"

sudo chown -R $(id -u):$(id -g) "/work/" || true

# ---------- basic info architecture ----------
log "Executing ./.devcontainer/setup.sh"
ARCH=$(uname -m)
log "Container architecture: $ARCH"

# ---------- prompt (add once) ----------
PROMPT_EXPORT="export PS1='\\u@\\h:\\w\\$ '"
log "Ensuring prompt…"
append_once "$PROMPT_EXPORT" "$PROMPT_EXPORT"

# ---------- sanity checks ----------
command -v uv > /dev/null 2>&1 || die "uv not found in PATH"
[ -d "$PROJECT_DIR" ] || die "project directory not found: $PROJECT_DIR"

# ---------- venv (recreate & sync) ----------
log "Creating venv with system site-packages: $VENV_DIR"
UV_VENV_CLEAR=1 uv venv --system-site-packages "$VENV_DIR"

log "Syncing dependencies from pyproject in $PROJECT_DIR"
uv sync --directory "$PROJECT_DIR"

# ---------- auto-activation for interactive shells ----------
log "Configuring auto-activation in $BASHRC"
append_once \
    "if [[ \$- == *i* ]] && [[ -z \${VIRTUAL_ENV:-} ]] && [[ -f $VENV_DIR/bin/activate ]]; then source $VENV_DIR/bin/activate; fi" \
    "
# Auto-activate lazer venv in interactive shells
if [[ \$- == *i* ]] && [[ -z \${VIRTUAL_ENV:-} ]] && [[ -f $VENV_DIR/bin/activate ]]; then
  source $VENV_DIR/bin/activate
fi"
uv pip install --python "/$VENV_DIR/bin/python" --project . /work/tools/expkits-ci
EXPKITS_ARG_EVAL="eval \"\$(register-python-argcomplete expkits-ci)\""
append_once "$EXPKITS_ARG_EVAL" "$EXPKITS_ARG_EVAL"

# -------- PLUMBER ---------
uv pip install --python "/$VENV_DIR/bin/python" --project . /work/tools/plumber

# ---------- ONNX ----------

ARCH=$(uname -m)

if [ "$ARCH" == "x86_64" ]; then
    ORT_URL="https://github.com/microsoft/onnxruntime/releases/download/v1.18.1/onnxruntime-linux-x64-1.18.1.tgz"
elif [ "$ARCH" == "aarch64" ]; then
    ORT_URL="https://github.com/microsoft/onnxruntime/releases/download/v1.18.1/onnxruntime-linux-aarch64-1.18.1.tgz"
else
    echo "Unsupported architecture: $ARCH"
    exit 1
fi

# Download and extract ONNX Runtime only if not already present
if [ ! -d "deps/onnxruntime" ]; then
    log "ONNX Runtime not found, downloading and extracting..."
    wget "$ORT_URL"
    tar xf "$(basename $ORT_URL)"

    # Extract directory name
    DIR_NAME=$(basename $ORT_URL .tgz)

    # Install locally (no sudo!)
    mkdir -p deps/onnxruntime
    cp -r "$DIR_NAME/include" deps/onnxruntime/
    cp -r "$DIR_NAME/lib" deps/onnxruntime/
else
    log "ONNX Runtime already present, skipping download and extraction."
fi

if [ ! -f "/work/deps/plantuml-mit-1.2026.2.jar" ]; then
    log "PlantUML JAR not found, downloading..."
    mkdir -p /work/deps
    wget https://github.com/plantuml/plantuml/releases/download/v1.2026.2/plantuml-mit-1.2026.2.jar -O /work/deps/plantuml-mit-1.2026.2.jar
else
    log "PlantUML JAR already present, skipping download."
fi

log "Installing pre-commit hooks"
cd /work && pre-commit install && pre-commit install -t commit-msg

log "Done. Open a NEW terminal to see the prompt & venv activation."
