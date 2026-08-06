#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Usage: ./scripts/peksink-web.sh <generate|check|test>

Commands:
  generate  Rebuild the committed browser bundle.
  check     Rebuild temporarily and fail when the bundle has drifted.
  test      Run the WebUI unit tests.
EOF
}

if [[ $# -ne 1 || "$1" == "-h" || "$1" == "--help" ]]; then
    usage
    [[ $# -eq 1 ]] && exit 0
    exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"
detect_script="${repo_root}/scripts/quick-start/detect-environment.sh"

if [[ "${PEK_WEB_IN_CONTAINER:-0}" != "1" && ! -f /.dockerenv && "${repo_root}" != "/work" ]]; then
    if ! command -v docker > /dev/null 2>&1; then
        echo "docker is required on the host; peksink WebUI commands run inside the PEK container" >&2
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

    exec docker exec --user dev --workdir /work --env PEK_WEB_IN_CONTAINER=1 \
        "${container}" ./scripts/peksink-web.sh "$@"
fi

cd "${repo_root}"

if ! command -v npm > /dev/null 2>&1; then
    echo "npm is required inside the PEK development container" >&2
    exit 127
fi

case "$1" in
    generate | check)
        exec npm --prefix development/web run "$1"
        ;;
    test)
        exec npm --prefix development/web test
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac
