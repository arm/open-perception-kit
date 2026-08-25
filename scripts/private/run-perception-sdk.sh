#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
detect_script="${repo_root}/scripts/quick-start/detect-environment.sh"

if [[ "${PEK_PERCEPTION_IN_CONTAINER:-0}" != "1" && ! -f /.dockerenv && "${repo_root}" != "/work" ]]; then
    if ! command -v docker > /dev/null 2>&1; then
        echo "docker is required on the host; Perception SDK commands run inside the PEK container" >&2
        exit 127
    fi

    container="${PEK_CONTAINER:-}"
    if [[ -z "${container}" ]]; then
        if ! detect_output="$("${detect_script}" --shell)"; then
            eval "${detect_output}"
            echo "unsupported host platform: ${PEK_PLATFORM_NAME:-unknown}" >&2
            if [[ -n "${PEK_UNSUPPORTED_REASON:-}" ]]; then
                echo "reason: ${PEK_UNSUPPORTED_REASON}" >&2
            fi
            exit 1
        fi
        eval "${detect_output}"
        container="${PEK_CONTAINER_NAME}"
    fi

    if ! docker inspect -f '{{.State.Running}}' "${container}" 2> /dev/null | grep -q '^true$'; then
        echo "container '${container}' is not running" >&2
        echo "start it with ./scripts/quick_start.sh or override it with PEK_CONTAINER" >&2
        exit 1
    fi

    exec docker exec --user dev --workdir /work --env PEK_PERCEPTION_IN_CONTAINER=1 \
        "${container}" ./scripts/perception-sdk.sh "$@"
fi

export GIT_CONFIG_COUNT=2
export GIT_CONFIG_KEY_0=safe.directory
export GIT_CONFIG_VALUE_0="${repo_root}"
export GIT_CONFIG_KEY_1=safe.directory
export GIT_CONFIG_VALUE_1="${repo_root}/tools/flowdata-sdk"

cd "${repo_root}"
exec python3 tools/perception/cli.py "$@"
