#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

# --- Config (edit as needed) ---
: "${PEK_EXECUTORCH:=1}" # default if not already set; change to a path/value if needed

# --- Resolve paths relative to THIS script's location ---
SELF_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_SH="$SELF_DIR/../../tools/executorchbuild/build.sh"
GETLIB_SH="$SELF_DIR/../../tools/executorchbuild/getlibthorch.sh"

# --- Basic checks ---
[[ -f "$BUILD_SH" ]] || {
    echo "Missing: $BUILD_SH" >&2
    exit 1
}
[[ -f "$GETLIB_SH" ]] || {
    echo "Missing: $GETLIB_SH" >&2
    exit 1
}

run_in_script_dir() {
    local script="$1"
    local script_dir
    script_dir="$(cd -- "$(dirname -- "$script")" && pwd)"
    (cd -- "$script_dir" && bash "./$(basename -- "$script")")
}

echo "PEK_EXECUTORCH=$PEK_EXECUTORCH"
echo "1) Running build.sh as root (in its own directory)..."
sudo --preserve-env=PEK_EXECUTORCH bash -c '
  set -euo pipefail
  script="$1"
  script_dir="$(cd -- "$(dirname -- "$script")" && pwd)"
  cd -- "$script_dir"
  bash "./$(basename -- "$script")"
' _ "$BUILD_SH"

echo "2) Running getlibthorch.sh as normal user (in its own directory)..."
run_in_script_dir "$GETLIB_SH"

echo "Done."
