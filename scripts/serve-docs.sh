#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

DOCS_DIR="/work/docs/corespec/html"
PORT=8080

if [ ! -d "$DOCS_DIR" ]; then
    echo "Documentation directory not found: $DOCS_DIR"
    exit 1
fi

cd "$DOCS_DIR"
echo "Serving documentation from $DOCS_DIR on http://localhost:$PORT"
python3 -m http.server "$PORT"
