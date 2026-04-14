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
TOOLS_DIR="/work/tools"
VENV_DIR="$TOOLS_DIR/.venv"
BASHRC="$HOME/.bashrc"
IMAGE_DEVTOOLS_VENV="${AMP_DEVTOOLS_VENV:-/opt/amp-venvs/devtools}"

log "Executing ./.devcontainer/devsetup.sh (dev extras)"

# First run the shared base setup (ONNX, PlantUML, chown, etc.)
./.devcontainer/setup.sh

# ---------- prompt (add once) ----------
PROMPT_EXPORT="export PS1='\\u@\\h:\\w\\$ '"
log "Ensuring prompt…"
append_once "$PROMPT_EXPORT" "$PROMPT_EXPORT"

# ---------- sanity checks ----------
[ -d "$TOOLS_DIR" ] || die "tools directory not found: $TOOLS_DIR"

# ---------- venv wiring (no installs) ----------
if [[ -d "$IMAGE_DEVTOOLS_VENV" ]]; then
    log "Using image-provided devtools venv: $IMAGE_DEVTOOLS_VENV"
    if [[ ! -e "$VENV_DIR" ]]; then
        ln -s "$IMAGE_DEVTOOLS_VENV" "$VENV_DIR"
    fi
else
    die "Image devtools venv not found at $IMAGE_DEVTOOLS_VENV. Install it via Dockerfile."
fi

# ---------- auto-activation for interactive shells ----------
log "Configuring auto-activation in $BASHRC"
append_once \
    "if [[ \$- == *i* ]] && [[ -z \${VIRTUAL_ENV:-} ]] && [[ -f $VENV_DIR/bin/activate ]]; then source $VENV_DIR/bin/activate; fi" \
    "
# Auto-activate shared tools venv in interactive shells
if [[ \$- == *i* ]] && [[ -z \${VIRTUAL_ENV:-} ]] && [[ -f $VENV_DIR/bin/activate ]]; then
  source $VENV_DIR/bin/activate
fi"

log "Configuring AMP terminal welcome in $BASHRC"
append_once \
    "# Show AMP terminal welcome in interactive bash shells" \
    "
# Show AMP terminal welcome in interactive bash shells
if [[ \$- == *i* ]] && [[ -z \${AMP_TERMINAL_INIT_ACTIVE:-} ]] && [[ -f /work/scripts/private/amp-terminal-init.sh ]]; then
  AMP_TERMINAL_INIT_SKIP_BASHRC=1
  source /work/scripts/private/amp-terminal-init.sh
  unset AMP_TERMINAL_INIT_SKIP_BASHRC
fi"
EXPKITS_ARG_EVAL='eval "$(register-python-argcomplete expkits-ci)"'
append_once "$EXPKITS_ARG_EVAL" "$EXPKITS_ARG_EVAL"

log "Installing pre-commit hooks"
cd /work && pre-commit install && pre-commit install -t commit-msg

# -------- PLUMBER ---------
log "Done. Open a NEW terminal to see the prompt & venv activation."
