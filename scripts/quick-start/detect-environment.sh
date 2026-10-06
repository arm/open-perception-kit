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
# Detects the host platform used by the public quick-start flow.
#
# Usage:
#   scripts/quick-start/detect-environment.sh [--shell]
#
# Default output is human-readable. --shell emits KEY=VALUE lines suitable for
# eval by other quick-start scripts.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Usage:
  detect-environment.sh [--shell] [-h|--help]

Detects the quick-start host environment.

Supported host classes:
  rpi5         Raspberry Pi 5 running Linux
  linux-x86_64 Generic x86_64 Linux host
  linux-aarch64 Debian or Ubuntu Arm64 Linux host
  wsl          Windows Subsystem for Linux
  macos        macOS host

Options:
  --shell      Print shell assignments instead of human-readable output
  -h, --help   Show this help
EOF
}

shell_escape() {
    printf "%q" "$1"
}

read_file_or_empty() {
    local path="$1"

    if [[ -r "$path" ]]; then
        tr -d '\0' < "$path"
    fi
}

detect_os_release_field() {
    local field="$1"

    if [[ -r /etc/os-release ]]; then
        (   
            . /etc/os-release
            printf "%s" "${!field:-}"
        )
    fi
}

is_wsl() {
    local text=""

    if [[ -r /proc/sys/kernel/osrelease ]]; then
        text+="$(< /proc/sys/kernel/osrelease)"
    fi
    if [[ -r /proc/version ]]; then
        text+=" $(< /proc/version)"
    fi

    [[ "$text" =~ [Mm]icrosoft|WSL ]]
}

detect_rpi_model() {
    read_file_or_empty /proc/device-tree/model
}

detect_environment() {
    OPK_UNAME_S="$(uname -s)"
    OPK_UNAME_M="$(uname -m)"
    OPK_PRETTY_OS="$(detect_os_release_field PRETTY_NAME)"
    OPK_OS_ID="$(detect_os_release_field ID)"
    OPK_OS_VERSION_ID="$(detect_os_release_field VERSION_ID)"
    OPK_OS_VERSION_CODENAME="$(detect_os_release_field VERSION_CODENAME)"
    OPK_RPI_MODEL="$(detect_rpi_model)"
    OPK_IN_CONTAINER="false"

    if [[ -f /.dockerenv ]] || grep -qaE '/docker/|/containers/' /proc/1/cgroup 2> /dev/null; then
        OPK_IN_CONTAINER="true"
    fi

    OPK_PLATFORM_ID="unsupported"
    OPK_PLATFORM_NAME="Unsupported platform"
    OPK_CONTAINER_SERVICE=""
    OPK_CONTAINER_NAME=""
    OPK_SUPPORTED="false"
    OPK_UNSUPPORTED_REASON=""
    OPK_DEV_CONTAINER_NAME="${OPK_DEV_CONTAINER_NAME:-}"
    OPK_PICAMERA="disabled"
    if [[ -n "${OPK_QUICK_START_CI_NAME:-}" ]]; then
        OPK_DEV_CONTAINER_NAME="${OPK_QUICK_START_CI_NAME}-dev"
    fi

    case "$OPK_UNAME_S" in
        Darwin)
            OPK_PLATFORM_ID="macos"
            OPK_PLATFORM_NAME="macOS"
            OPK_CONTAINER_SERVICE="opk-dev"
            OPK_CONTAINER_NAME="${OPK_DEV_CONTAINER_NAME:-open-perception-kit}"
            OPK_SUPPORTED="true"
            ;;
        Linux)
            if is_wsl; then
                OPK_PLATFORM_ID="wsl"
                OPK_PLATFORM_NAME="Windows Subsystem for Linux"
                OPK_CONTAINER_SERVICE="opk-dev"
                OPK_CONTAINER_NAME="${OPK_DEV_CONTAINER_NAME:-open-perception-kit}"
                OPK_SUPPORTED="true"
            elif grep -qi "raspberry pi 5" <<< "$OPK_RPI_MODEL"; then
                OPK_PICAMERA="enabled"
                OPK_PLATFORM_ID="rpi5"
                OPK_PLATFORM_NAME="Raspberry Pi 5"
                OPK_CONTAINER_SERVICE="opk-dev"
                OPK_CONTAINER_NAME="${OPK_DEV_CONTAINER_NAME:-open-perception-kit}"
                OPK_SUPPORTED="true"
            elif [[ "$OPK_UNAME_M" == "x86_64" || "$OPK_UNAME_M" == "amd64" ]]; then
                OPK_PLATFORM_ID="linux-x86_64"
                OPK_PLATFORM_NAME="Linux x86_64"
                OPK_CONTAINER_SERVICE="opk-dev"
                OPK_CONTAINER_NAME="${OPK_DEV_CONTAINER_NAME:-open-perception-kit}"
                OPK_SUPPORTED="true"
            elif [[ "$OPK_UNAME_M" == "aarch64" || "$OPK_UNAME_M" == "arm64" ]] &&
                [[ "$OPK_OS_ID" == "debian" || "$OPK_OS_ID" == "ubuntu" ]]; then
                OPK_PLATFORM_ID="linux-aarch64"
                OPK_PLATFORM_NAME="Linux Arm64"
                OPK_CONTAINER_SERVICE="opk-dev"
                OPK_CONTAINER_NAME="${OPK_DEV_CONTAINER_NAME:-open-perception-kit}"
                OPK_SUPPORTED="true"
            else
                OPK_UNSUPPORTED_REASON="Linux host is not Raspberry Pi 5, x86_64, or Debian/Ubuntu Arm64."
            fi
            ;;
        *)
            OPK_UNSUPPORTED_REASON="Kernel '$OPK_UNAME_S' is not supported by the quick-start flow."
            ;;
    esac
}

print_shell() {
    local names=(
        OPK_SUPPORTED
        OPK_PLATFORM_ID
        OPK_PLATFORM_NAME
        OPK_CONTAINER_SERVICE
        OPK_CONTAINER_NAME
        OPK_DEV_CONTAINER_NAME
        OPK_PICAMERA
        OPK_UNAME_S
        OPK_UNAME_M
        OPK_PRETTY_OS
        OPK_OS_ID
        OPK_OS_VERSION_ID
        OPK_OS_VERSION_CODENAME
        OPK_RPI_MODEL
        OPK_IN_CONTAINER
        OPK_UNSUPPORTED_REASON
    )
    local name

    for name in "${names[@]}"; do
        printf "%s=%s\n" "$name" "$(shell_escape "${!name}")"
    done
}

print_human() {
    echo "Detected environment:"
    echo "  Platform: ${OPK_PLATFORM_NAME} (${OPK_PLATFORM_ID})"
    echo "  Kernel:   ${OPK_UNAME_S}"
    echo "  Machine:  ${OPK_UNAME_M}"

    if [[ -n "$OPK_PRETTY_OS" ]]; then
        echo "  OS:       ${OPK_PRETTY_OS}"
    fi
    if [[ -n "$OPK_RPI_MODEL" ]]; then
        echo "  Device:   ${OPK_RPI_MODEL}"
    fi
    echo "  Container shell: ${OPK_IN_CONTAINER}"

    if [[ "$OPK_SUPPORTED" == "true" ]]; then
        echo
        echo "Quick-start target:"
        echo "  Compose service: ${OPK_CONTAINER_SERVICE}"
        echo "  Container name:  ${OPK_CONTAINER_NAME}"
    else
        echo
        echo "Unsupported platform."
        if [[ -n "$OPK_UNSUPPORTED_REASON" ]]; then
            echo "Reason: ${OPK_UNSUPPORTED_REASON}"
        fi
    fi
}

main() {
    local output="human"

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --shell)
                output="shell"
                ;;
            -h | --help)
                usage
                exit 0
                ;;
            *)
                echo "Error: unknown argument '$1'" >&2
                echo >&2
                usage >&2
                exit 2
                ;;
        esac
        shift
    done

    detect_environment

    if [[ "$output" == "shell" ]]; then
        print_shell
    else
        print_human
    fi

    if [[ "$OPK_SUPPORTED" != "true" ]]; then
        exit 1
    fi
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    main "$@"
fi
