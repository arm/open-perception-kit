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
requested_project_root="${OPK_PROJECT_ROOT:-$SCRIPT_DIR/..}"
if [[ "$requested_project_root" != /* ]]; then
    echo "OPK_PROJECT_ROOT must be an absolute path: $requested_project_root" >&2
    exit 2
fi
if [[ ! -d "$requested_project_root" ]]; then
    echo "OPK project root does not exist: $requested_project_root" >&2
    exit 2
fi
OPK_PROJECT_ROOT="$(cd -- "$requested_project_root" && pwd -P)"
export OPK_PROJECT_ROOT

DOCS_DIR="$OPK_PROJECT_ROOT/docs/html"
PORT=8080

if [ ! -d "$DOCS_DIR" ]; then
    echo "Documentation directory not found: $DOCS_DIR"
    exit 1
fi

cd "$DOCS_DIR"
echo "Serving documentation from $DOCS_DIR on http://localhost:$PORT"
python3 -m http.server "$PORT"
