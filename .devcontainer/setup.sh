#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

# ---------- helpers ----------
log() { echo -e "[setup.sh] $*"; }
die() {
        echo -e "[setup.sh] ERROR: $*" >&2
                                                         exit 1
}

trap 'die "failed at line $LINENO"' ERR

sudo chown -R $(id -u):$(id -g) "/work/" || true

# ---------- basic info ----------
log "Executing ./.devcontainer/setup.sh (base setup)"
ARCH=$(uname -m)
log "Container architecture: $ARCH"

# ---------- ONNX Runtime (verify only) ----------
ORT_DIR="/opt/pek-deps/onnxruntime"

if [[ -d "$ORT_DIR/include" && -d "$ORT_DIR/lib" ]]; then
    log "Found ONNX Runtime in image: $ORT_DIR"
else
    die "ONNX Runtime not found at $ORT_DIR. Install it via Dockerfile."
fi

# ---------- PlantUML JAR (verify only) ----------
WORK_PLANTUML_JAR="/work/deps/plantuml-mit-1.2026.2.jar"
IMAGE_PLANTUML_JAR="/opt/pek-deps/plantuml-mit-1.2026.2.jar"

if [[ -f "$WORK_PLANTUML_JAR" ]]; then
    log "Found PlantUML JAR in workspace: $WORK_PLANTUML_JAR"
elif [[ -f "$IMAGE_PLANTUML_JAR" ]]; then
    log "Found PlantUML JAR in image: $IMAGE_PLANTUML_JAR"
else
    log "PlantUML JAR not found in workspace or image. Docs generation may skip PlantUML figures."
fi

log "Base setup.sh finished."
