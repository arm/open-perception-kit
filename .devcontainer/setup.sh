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

# ---------- basic info architecture ----------
log "Executing ./.devcontainer/setup.sh (base setup)"
ARCH=$(uname -m)
log "Container architecture: $ARCH"

# ---------- ONNX Runtime (shared for dev and deployment) ----------

ARCH=$(uname -m)

if [ "$ARCH" == "x86_64" ]; then
    ORT_URL="https://github.com/microsoft/onnxruntime/releases/download/v1.18.1/onnxruntime-linux-x64-1.18.1.tgz"
elif [ "$ARCH" == "aarch64" ]; then
    ORT_URL="https://github.com/microsoft/onnxruntime/releases/download/v1.18.1/onnxruntime-linux-aarch64-1.18.1.tgz"
else
    echo "Unsupported architecture: $ARCH"
    exit 1
fi

ORT_TGZ=$(basename "$ORT_URL")

# Download and extract ONNX Runtime only if not already present
if [ ! -d "deps/onnxruntime" ]; then
    log "ONNX Runtime not found, downloading and extracting..."
    if [ ! -f "$ORT_TGZ" ]; then
        wget "$ORT_URL"
    else
        log "ONNX Runtime archive $ORT_TGZ already present, skipping download."
    fi
    tar xf "$ORT_TGZ"

    # Extract directory name
    DIR_NAME=$(basename $ORT_URL .tgz)

    # Install locally (no sudo!)
    mkdir -p deps/onnxruntime
    cp -r "$DIR_NAME/include" deps/onnxruntime/
    cp -r "$DIR_NAME/lib" deps/onnxruntime/
else
    log "ONNX Runtime already present, skipping download and extraction."
fi

if [ ! -f "/work/deps/plantuml-mit-1.2026.2.jar" ]; then
    log "PlantUML JAR not found, downloading..."
    mkdir -p /work/deps
    wget https://github.com/plantuml/plantuml/releases/download/v1.2026.2/plantuml-mit-1.2026.2.jar -O /work/deps/plantuml-mit-1.2026.2.jar
else
    log "PlantUML JAR already present, skipping download."
fi

log "Base setup.sh finished."
