#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${OPK_PROJECT_ROOT:-$SCRIPT_DIR/..}"
if [[ "$PROJECT_ROOT" != /* || ! -d "$PROJECT_ROOT/docs/public" ]]; then
    echo "OPK_PROJECT_ROOT must be an absolute OPK checkout path." >&2
    exit 2
fi
DOCS_DIR="$PROJECT_ROOT/docs/html"
PORT="${DOCS_PORT:-3003}"
if [[ ! -f "$DOCS_DIR/index.html" ]]; then
    echo "Generated documentation is missing. Run ./scripts/gen-doc.sh first." >&2
    exit 1
fi
printf 'Serving generated documentation at http://localhost:%s; rerun gen-doc.sh after edits.\n' "$PORT"
exec python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$DOCS_DIR"
