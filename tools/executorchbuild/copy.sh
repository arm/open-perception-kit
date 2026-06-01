#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

# Where executorch source + build are
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
EXECUTORCH_DIR="${EXECUTORCH_DIR:-${SCRIPT_DIR}/executorch}"
BUILD_DIR="${BUILD_DIR:-${EXECUTORCH_DIR}/build}"

# Where to stage the "dev deps"
DEST_DIR="${DEST_DIR:-/work/deps/executorch}"

# Ownership for staged files
OWNER_USER="${OWNER_USER:-devgoblin}"
OWNER_GROUP="${OWNER_GROUP:-devgoblin}"

if [[ ! -d "$EXECUTORCH_DIR" ]]; then
    echo "ERROR: EXECUTORCH_DIR not found: $EXECUTORCH_DIR" >&2
    exit 1
fi

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "ERROR: BUILD_DIR not found: $BUILD_DIR" >&2
    echo "Hint: build first (cmake -S . -B build && cmake --build build)" >&2
    exit 1
fi

if ! id "$OWNER_USER" >/dev/null 2>&1; then
    echo "ERROR: user '$OWNER_USER' does not exist in this container." >&2
    exit 1
fi

# 1) Delete + recreate destination
rm -rf "$DEST_DIR"
install -d -m 0755 -o "$OWNER_USER" -g "$OWNER_GROUP" "$DEST_DIR"

# 2) Install everything needed for C++ development/runtime into DEST_DIR
# This uses executorch's own install rules (headers, libs, cmake config, etc.)
cmake --install "$BUILD_DIR" --prefix "$DEST_DIR"

# 3) Ensure ownership is devgoblin (some install steps may write as root)
chown -R "$OWNER_USER:$OWNER_GROUP" "$DEST_DIR"

echo "Staged ExecuTorch C++ deps to: $DEST_DIR"
echo "Owner: $OWNER_USER:$OWNER_GROUP"
