#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
# Updates the persisted Playwright report site used by GitHub Pages.
################################################################

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "${SCRIPT_DIR}/publish_playwright_pages.py" "$@"
