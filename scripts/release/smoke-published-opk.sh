#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
# Runs the published OPK archive through the release smoke pipeline.
################################################################

set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <image> <commit> <archive.tar.gz>" >&2
    exit 2
fi

image="$1"
commit="$2"
archive="$(realpath "$3")"
archive_dir="$(dirname "$archive")"
archive_name="$(basename "$archive")"
python_operation="$(realpath development/tests/python_script_op/runtime_environment.py)"

docker pull "$image"
test "$(docker image inspect --format '{{ index .Config.Labels "org.opencontainers.image.revision" }}' "$image")" = "$commit"
trap 'docker image rm "$image" 2>/dev/null || true' EXIT

docker run --rm --network none --entrypoint bash \
    --mount "type=bind,source=$archive_dir,target=/published,readonly" \
    --mount "type=bind,source=$python_operation,target=/runtime_environment.py,readonly" \
    "$image" /work/scripts/release/smoke-opk-package.sh \
    "/published/$archive_name" /runtime_environment.py
