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

OPK_PROJECT_ROOT="$(cd "${OPK_PROJECT_ROOT:-.}" && pwd -P)"
export OPK_PROJECT_ROOT

# ---------- helpers ----------
log() { echo -e "[setup.sh] $*"; }
die() {
    echo -e "[setup.sh] ERROR: $*" >&2
    exit 1
}

trap 'die "failed at line $LINENO"' ERR

# !!! WARNING: HOST WORKSPACE OWNERSHIP HAZARD !!!
# This line recursively rewrites ownership of the project root. In CI, it is
# often a bind-mounted checkout from the self-hosted runner host.
# Reusing this pattern without isolating the checkout path and the compose
# project can poison later jobs and break actions/checkout with permission
# errors such as .git/index.lock or unlink failures on tracked files.
# Read the full incident note before changing or reusing this line:
#   .github/ci/self-hosted-runner-workspace-isolation.md
sudo chown -R "$(id -u):$(id -g)" "${OPK_PROJECT_ROOT}/" || true
if [[ -x /usr/local/bin/development-entrypoint ]]; then
    /usr/local/bin/development-entrypoint --seed-artifacts
fi

# ---------- basic info ----------
log "Executing ./.devcontainer/setup.sh (base setup)"
ARCH=$(uname -m)
log "Container architecture: $ARCH"

# ---------- ONNX Runtime (verify only) ----------
ORT_DIR="/opt/opk-deps/onnxruntime"

if [[ -d "$ORT_DIR/include" && -d "$ORT_DIR/lib" ]]; then
    log "Found ONNX Runtime in image: $ORT_DIR"
else
    die "ONNX Runtime not found at $ORT_DIR. Install it via Dockerfile."
fi

# ---------- PlantUML JAR (verify only) ----------
WORK_PLANTUML_JAR="${OPK_PROJECT_ROOT}/deps/plantuml-mit-1.2026.2.jar"
IMAGE_PLANTUML_JAR="/opt/opk-deps/plantuml-mit-1.2026.2.jar"

if [[ -f "$WORK_PLANTUML_JAR" ]]; then
    log "Found PlantUML JAR in workspace: $WORK_PLANTUML_JAR"
elif [[ -f "$IMAGE_PLANTUML_JAR" ]]; then
    log "Found PlantUML JAR in image: $IMAGE_PLANTUML_JAR"
else
    log "PlantUML JAR not found in workspace or image. Docs generation may skip PlantUML figures."
fi

log "Base setup.sh finished."
