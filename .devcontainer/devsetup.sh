#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################
set -euo pipefail

# ---------- helpers ----------
log() { echo -e "[devsetup.sh] $*"; }
die() {
    echo -e "[devsetup.sh] ERROR: $*" >&2
               exit 1
}

append_once() {
     local needle="$1"
     local line="$2"
     grep -Fqx "$needle" "$BASHRC" 2> /dev/null || echo "$line" >> "$BASHRC"
}

trap 'die "failed at line $LINENO"' ERR

# ---------- config (dev-only) ----------
PROJECT_DIR="/work/tools/lazer"
VENV_DIR="$PROJECT_DIR/.venv"
BASHRC="$HOME/.bashrc"

log "Executing ./.devcontainer/devsetup.sh (dev extras)"

# First run the shared base setup (ONNX, PlantUML, chown, etc.)
./.devcontainer/setup.sh

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

log "Installing expkits-ci and argcomplete integration"
uv pip install --python "/$VENV_DIR/bin/python" --project . /work/tools/expkits-ci
EXPKITS_ARG_EVAL='eval "$(register-python-argcomplete expkits-ci)"'
append_once "$EXPKITS_ARG_EVAL" "$EXPKITS_ARG_EVAL"

log "Installing pre-commit hooks"
cd /work && pre-commit install && pre-commit install -t commit-msg

log "Done. Open a NEW terminal to see the prompt & venv activation."
