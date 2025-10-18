#!/usr/bin/env bash
set -e

# Get the absolute path of the script's directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Use that to build paths
SRC_FILES=(
  "$SCRIPT_DIR/basic_proc.cpp"
  "$SCRIPT_DIR/cpu_imgproc.cpp"
  "$SCRIPT_DIR/tensor_types.cpp"
  "$SCRIPT_DIR/uniflow.cpp"
)
OUT_FILE="$SCRIPT_DIR/../build/uniflow.mjs"

# Now compile using absolute paths
em++ "${SRC_FILES[@]}" -O3 -std=c++17 \
  -sWASM=1 \
  -sMODULARIZE=1 -sEXPORT_ES6=1 \
  -sEXPORTED_FUNCTIONS='[]' \
  -sEXPORTED_RUNTIME_METHODS='["cwrap"]' \
  -o "$OUT_FILE"