#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
requested_project_root="${PEK_PROJECT_ROOT:-$SCRIPT_DIR/..}"
if [[ "$requested_project_root" != /* ]]; then
    echo "PEK_PROJECT_ROOT must be an absolute path: $requested_project_root" >&2
    exit 2
fi
if [[ ! -d "$requested_project_root" ]]; then
    echo "PEK project root does not exist: $requested_project_root" >&2
    exit 2
fi
PEK_PROJECT_ROOT="$(cd -- "$requested_project_root" && pwd -P)"
export PEK_PROJECT_ROOT

DOCS_DIR="$PEK_PROJECT_ROOT/docs/html"
PORT=8080

if [ ! -d "$DOCS_DIR" ]; then
    echo "Documentation directory not found: $DOCS_DIR"
    exit 1
fi

cd "$DOCS_DIR"
echo "Serving documentation from $DOCS_DIR on http://localhost:$PORT"
python3 -m http.server "$PORT"
