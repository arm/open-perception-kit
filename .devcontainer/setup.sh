#!/usr/bin/env bash
set -euo pipefail

# ---------- helpers ----------
log() { echo -e "[setup.sh] $*"; }
die() { echo -e "[setup.sh] ERROR: $*" >&2; exit 1; }

append_once() {
  local needle="$1"
  local line="$2"
  grep -Fqx "$needle" "$BASHRC" 2>/dev/null || echo "$line" >> "$BASHRC"
}

trap 'die "failed at line $LINENO"' ERR

#log "Running on: $HOST_OS OS"

# ---------- display ---------
#case "${HOST_OS:-}" in
#  Linux*|linux*)
#    log "🖥️ Setting display for Linux"
#    export DISPLAY="${HOST_DISPLAY:-:0}"
#    ;;
#  Darwin*|darwin*)
#    log "🖥️ Setting display for MacOS"
#    export DISPLAY="${HOST_DISPLAY:-host.docker.internal:0}"
#    ;;
#  *)
#    log "🖥️ Unknown host OS, no display setup"
#    ;;
#esac

# ---------- config ----------
PROJECT_DIR="/work/tools/lazer"
VENV_DIR="$PROJECT_DIR/.venv"
BASHRC="$HOME/.bashrc"

# ---------- basic info architecture ----------
log "Executing ./.devcontainer/setup.sh"
ARCH=$(uname -m)
log "Container architecture: $ARCH"

# ---------- prompt (add once) ----------
PROMPT_EXPORT="export PS1='\\u@\\h:\\w\\$ '"
log "Ensuring prompt…"
append_once "$PROMPT_EXPORT" "$PROMPT_EXPORT"

# ---------- sanity checks ----------
command -v uv >/dev/null 2>&1 || die "uv not found in PATH"
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

# ---------- INTEL DRIVERS ----------

if [ "$CONTAINER_TYPE" == "intel_igpu_pc" ]; then
  set -eux; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
    gstreamer1.0-vaapi intel-media-va-driver \
    libdrm-dev libgbm-dev libegl1-mesa-dev libgles2-mesa-dev \
    libwayland-dev libva-dev libv4l-dev libgtk-3-0; \
    vainfo intel-media-va-driver gstreamer1.0-vaapi libva-drm2; 
  rm -rf /var/lib/apt/lists/*
fi

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

# Download and extract
wget "$ORT_URL"
tar xf "$(basename $ORT_URL)"

# Extract directory name
DIR_NAME=$(basename $ORT_URL .tgz)

# Install locally (no sudo!)
mkdir -p deps/onnxruntime
cp -r "$DIR_NAME/include" deps/onnxruntime/
cp -r "$DIR_NAME/lib" deps/onnxruntime/

#!/usr/bin/env bash

# ---------- optional: only auto-activate inside project ----------
# If you prefer activation ONLY when you're in /work/tools/lazer*, replace the block above with:
# append_once \
#   'if [[ $- == *i* ]] && [[ -z ${VIRTUAL_ENV:-} ]] && [[ "$PWD" == '"$PROJECT_DIR"'* ]] && [[ -f '"$VENV_DIR"'/bin/activate ]]; then source '"$VENV_DIR"'/bin/activate; fi' \
#   "
# # Auto-activate lazer venv only inside the project dir
# if [[ \$- == *i* ]] && [[ -z \${VIRTUAL_ENV:-} ]] && [[ \"\$PWD\" == $PROJECT_DIR* ]] && [[ -f $VENV_DIR/bin/activate ]]; then
#   source $VENV_DIR/bin/activate
# fi"

# ---------- smoke tests (non-fatal) ----------
#log "Running smoke tests…"
#set +e
#"$VENV_DIR/bin/python" -c 'import sys; print("Python:", sys.version.split()[0])' >/dev/null 2>&1 || true
#"$VENV_DIR/bin/python" -c 'import gi; import gi.repository.Gst as _; print("gi/Gst OK")' >/dev/null 2>&1 || true
#if command -v lazer >/dev/null 2>&1; then
#  lazer --help >/dev/null 2>&1 || true
#fi
#set -e

export GST_PLUGIN_PATH=/work/development/build:/usr/lib/x86_64-linux-gnu/gstreamer-1.0

log "Done. Open a NEW terminal to see the prompt & venv activation."


