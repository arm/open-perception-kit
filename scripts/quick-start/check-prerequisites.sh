#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################
# Quick-start host prerequisite checks.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Usage:
  check-prerequisites.sh [--check-only] [-h|--help]

Checks the prerequisites for the detected quick-start platform.
Missing installable prerequisites are installed by default. Use --check-only to
report failures without installing anything.

Requirement format used below:
  id|check_function|fallback packages|description

The check function is the source of truth. Fallback packages are installed only
when the check fails, so tools installed from non-distro sources are accepted.
EOF
}

CHECK_ONLY="false"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --check-only)
            CHECK_ONLY="true"
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

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! detect_output="$("${SCRIPT_DIR}/detect-environment.sh" --shell)"; then
    eval "$detect_output"
    echo "Error: unsupported quick-start platform: ${PEK_PLATFORM_NAME:-unknown}" >&2
    if [[ -n "${PEK_UNSUPPORTED_REASON:-}" ]]; then
        echo "Reason: ${PEK_UNSUPPORTED_REASON}" >&2
    fi
    exit 1
fi
eval "$detect_output"

# Prerequisite check functions.
check_docker_cli() {
    command -v docker > /dev/null 2>&1
}

check_docker_compose() {
    command -v docker > /dev/null 2>&1 && docker compose version > /dev/null 2>&1
}

check_docker_access() {
    command -v docker > /dev/null 2>&1 && docker info > /dev/null 2>&1
}

check_hailo8_package() {
    dpkg-query -W -f='${Status}' hailo-all 2> /dev/null | grep -q "install ok installed"
}

check_hailo10_package() {
    dpkg-query -W -f='${Status}' hailo-h10-all 2> /dev/null | grep -q "install ok installed"
}

# Platform prerequisite lists. Keep most of these empty until the minimal sets
# are known. A requirement can have no fallback package when it needs user action
# instead of package installation, for example Docker group membership.
PREREQS_DEBIAN_13_X86=(
    "docker-cli|check_docker_cli|docker.io|Docker CLI"
    "docker-compose|check_docker_compose|docker-compose|Docker Compose plugin"
    "docker-access|check_docker_access||Docker daemon reachable by the current user"
)
PREREQS_UBUNTU_24_04_X86=(
    "docker-cli|check_docker_cli|docker.io|Docker CLI"
    "docker-compose|check_docker_compose|docker-compose-v2|Docker Compose plugin"
    "docker-access|check_docker_access||Docker daemon reachable by the current user"
)
PREREQS_UBUNTU_26_04_X86=(
    "docker-cli|check_docker_cli|docker.io|Docker CLI"
    "docker-compose|check_docker_compose|docker-compose-v2|Docker Compose plugin"
    "docker-access|check_docker_access||Docker daemon reachable by the current user"
)

PREREQS_WSL=(
    "docker-cli|check_docker_cli||Docker CLI from Docker Desktop WSL integration"
    "docker-compose|check_docker_compose||Docker Compose plugin from Docker Desktop WSL integration"
    "docker-access|check_docker_access||Docker Desktop engine reachable from WSL"
)
PREREQS_WSL_DEBIAN_13=()
PREREQS_WSL_UBUNTU_24_04=()
PREREQS_WSL_UBUNTU_26_04=()

PREREQS_RPI5=(
    "docker-cli|check_docker_cli|docker.io|Docker CLI"
    "docker-compose|check_docker_compose|docker-compose|Docker Compose plugin"
    "docker-access|check_docker_access||Docker daemon reachable by the current user"
)
PREREQS_RPI5_HAILO8=(
    "docker-cli|check_docker_cli|docker.io|Docker CLI"
    "docker-compose|check_docker_compose|docker-compose|Docker Compose plugin"
    "docker-access|check_docker_access||Docker daemon reachable by the current user"
    "hailo8-stack|check_hailo8_package|hailo-all|Hailo 8 software stack"
)
PREREQS_RPI5_HAILO10=(
    "docker-cli|check_docker_cli|docker.io|Docker CLI"
    "docker-compose|check_docker_compose|docker-compose|Docker Compose plugin"
    "docker-access|check_docker_access||Docker daemon reachable by the current user"
    "hailo10-stack|check_hailo10_package|hailo-h10-all|Hailo 10 software stack"
)
PREREQS_MACOS=(
    "docker-cli|check_docker_cli||Docker CLI from Docker Desktop"
    "docker-compose|check_docker_compose||Docker Compose plugin from Docker Desktop"
    "docker-access|check_docker_access||Docker Desktop engine reachable by the current user"
)
PREREQS_LINUX_X86=()

PACKAGE_MANAGER=""
SELECTED_PREREQ_ARRAYS=()
REQUIRED_PREREQS=()
MISSING_PACKAGES=()
FAILED_MANUAL_PREREQS=()

select_prereq_arrays() {
    case "$PEK_PLATFORM_ID" in
        macos)
            PACKAGE_MANAGER="brew"
            SELECTED_PREREQ_ARRAYS=(PREREQS_MACOS)
            ;;
        wsl)
            PACKAGE_MANAGER="manual"
            SELECTED_PREREQ_ARRAYS=(PREREQS_WSL)
            if [[ "$PEK_OS_ID" == "ubuntu" && "$PEK_OS_VERSION_ID" == "24.04" ]]; then
                SELECTED_PREREQ_ARRAYS+=(PREREQS_WSL_UBUNTU_24_04)
            elif [[ "$PEK_OS_ID" == "ubuntu" && "$PEK_OS_VERSION_ID" == "26.04" ]]; then
                SELECTED_PREREQ_ARRAYS+=(PREREQS_WSL_UBUNTU_26_04)
            elif [[ "$PEK_OS_ID" == "debian" && "$PEK_OS_VERSION_CODENAME" == "trixie" ]]; then
                SELECTED_PREREQ_ARRAYS+=(PREREQS_WSL_DEBIAN_13)
            fi
            ;;
        linux-x86_64)
            PACKAGE_MANAGER="apt"
            if [[ "$PEK_OS_ID" == "ubuntu" && "$PEK_OS_VERSION_ID" == "24.04" ]]; then
                SELECTED_PREREQ_ARRAYS=(PREREQS_UBUNTU_24_04_X86)
            elif [[ "$PEK_OS_ID" == "ubuntu" && "$PEK_OS_VERSION_ID" == "26.04" ]]; then
                SELECTED_PREREQ_ARRAYS=(PREREQS_UBUNTU_26_04_X86)
            elif [[ "$PEK_OS_ID" == "debian" && "$PEK_OS_VERSION_CODENAME" == "trixie" ]]; then
                SELECTED_PREREQ_ARRAYS=(PREREQS_DEBIAN_13_X86)
            else
                SELECTED_PREREQ_ARRAYS=(PREREQS_LINUX_X86)
            fi
            ;;
        rpi5 | rpi5-h8 | rpi5-h10)
            PACKAGE_MANAGER="apt"
            SELECTED_PREREQ_ARRAYS=(PREREQS_RPI5)
            if [[ "$PEK_PLATFORM_ID" == "rpi5-h10" ]]; then
                SELECTED_PREREQ_ARRAYS+=(PREREQS_RPI5_HAILO10)
            elif [[ "$PEK_PLATFORM_ID" == "rpi5-h8" ]]; then
                SELECTED_PREREQ_ARRAYS+=(PREREQS_RPI5_HAILO8)
            fi
            ;;
        *)
            echo "Error: no prerequisite profile for platform '${PEK_PLATFORM_ID}'." >&2
            exit 1
            ;;
    esac
}

append_unique_requirement() {
    local requirement="$1"
    local requirement_id="${requirement%%|*}"
    local existing

    for existing in "${REQUIRED_PREREQS[@]}"; do
        if [[ "${existing%%|*}" == "$requirement_id" ]]; then
            return
        fi
    done

    REQUIRED_PREREQS+=("$requirement")
}

append_unique_missing_package() {
    local pkg="$1"
    local existing

    [[ -z "$pkg" ]] && return

    for existing in "${MISSING_PACKAGES[@]}"; do
        if [[ "$existing" == "$pkg" ]]; then
            return
        fi
    done

    MISSING_PACKAGES+=("$pkg")
}

collect_required_prereqs() {
    local array_name
    local requirements
    local requirement

    REQUIRED_PREREQS=()

    for array_name in "${SELECTED_PREREQ_ARRAYS[@]}"; do
        eval 'requirements=("${'"$array_name"'[@]}")'
        for requirement in "${requirements[@]}"; do
            append_unique_requirement "$requirement"
        done
    done
}

collect_missing_prereqs() {
    local requirement
    local requirement_id
    local check_fn
    local packages
    local description
    local package_list
    local pkg

    MISSING_PACKAGES=()
    FAILED_MANUAL_PREREQS=()

    for requirement in "${REQUIRED_PREREQS[@]}"; do
        IFS='|' read -r requirement_id check_fn packages description <<< "$requirement"

        if "$check_fn"; then
            echo "OK: ${description}"
            continue
        fi

        echo "MISSING: ${description}"

        if [[ -z "$packages" ]]; then
            FAILED_MANUAL_PREREQS+=("$requirement")
            continue
        fi

        IFS=' ' read -r -a package_list <<< "$packages"
        for pkg in "${package_list[@]}"; do
            append_unique_missing_package "$pkg"
        done
    done
}

sudo_prefix() {
    if [[ "$(id -u)" -eq 0 ]]; then
        return
    fi

    if ! command -v sudo > /dev/null 2>&1; then
        echo "Error: sudo is required to install packages as a non-root user." >&2
        exit 1
    fi

    printf "sudo"
}

install_apt_packages() {
    local sudo_cmd

    if ! command -v apt-get > /dev/null 2>&1; then
        echo "Error: apt-get is required for package installation on this platform." >&2
        exit 1
    fi

    sudo_cmd="$(sudo_prefix)"

    if [[ -n "$sudo_cmd" ]]; then
        "$sudo_cmd" apt-get update
        "$sudo_cmd" apt-get install -y "${MISSING_PACKAGES[@]}"
    else
        apt-get update
        apt-get install -y "${MISSING_PACKAGES[@]}"
    fi
}

install_brew_packages() {
    if ! command -v brew > /dev/null 2>&1; then
        echo "Error: Homebrew is required for package installation on macOS." >&2
        exit 1
    fi

    brew install "${MISSING_PACKAGES[@]}"
}

install_missing_packages() {
    case "$PACKAGE_MANAGER" in
        apt)
            install_apt_packages
            ;;
        brew)
            install_brew_packages
            ;;
        *)
            echo "Error: unsupported package manager '${PACKAGE_MANAGER}'." >&2
            exit 1
            ;;
    esac
}

print_wsl_docker_desktop_hint() {
    echo "    Install and start Docker Desktop for Windows."
    echo "    Enable Settings > General > Use WSL 2 based engine."
    echo "    Enable Settings > Resources > WSL Integration for this distro."
    echo "    Reopen WSL, or run: wsl.exe --shutdown"
    echo "    Verify: docker info && docker compose version"
}

print_manual_failures() {
    local requirement
    local requirement_id
    local check_fn
    local packages
    local description
    local printed_wsl_docker_hint="false"

    [[ "${#FAILED_MANUAL_PREREQS[@]}" -eq 0 ]] && return

    echo
    echo "Prerequisites that need manual action:"
    for requirement in "${FAILED_MANUAL_PREREQS[@]}"; do
        IFS='|' read -r requirement_id check_fn packages description <<< "$requirement"
        echo "  ${description}"
        if [[ "$PEK_PLATFORM_ID" == "wsl" && "$requirement_id" == docker-* ]]; then
            if [[ "$printed_wsl_docker_hint" != "true" ]]; then
                print_wsl_docker_desktop_hint
                printed_wsl_docker_hint="true"
            fi
        elif [[ "$requirement_id" == "docker-access" ]]; then
            echo "    Start Docker and confirm 'docker info' works without sudo."
            if [[ "$PEK_PLATFORM_ID" == "linux-x86_64" || "$PEK_PLATFORM_ID" == rpi5* ]]; then
                echo "    On Linux, this often means: sudo usermod -aG docker \"\$USER\""
                echo "    Then log out and log back in."
            fi
        fi
    done
}

select_prereq_arrays
collect_required_prereqs

echo "Prerequisite checks:"
echo "  Platform: ${PEK_PLATFORM_NAME} (${PEK_PLATFORM_ID})"
echo "  Package manager: ${PACKAGE_MANAGER}"
echo "  Requirement arrays: ${SELECTED_PREREQ_ARRAYS[*]}"

if [[ "${#REQUIRED_PREREQS[@]}" -eq 0 ]]; then
    echo
    echo "No prerequisites are configured for this platform yet."
    exit 0
fi

echo
collect_missing_prereqs
print_manual_failures

if [[ "${#MISSING_PACKAGES[@]}" -eq 0 ]]; then
    if [[ "${#FAILED_MANUAL_PREREQS[@]}" -eq 0 ]]; then
        echo
        echo "All configured prerequisites are satisfied."
        exit 0
    fi
    exit 1
fi

echo
echo "Packages to install:"
printf '  %s\n' "${MISSING_PACKAGES[@]}"

if [[ "$CHECK_ONLY" == "true" ]]; then
    exit 1
fi

echo
echo "Installing missing packages..."
install_missing_packages

echo
echo "Rechecking prerequisites..."
collect_missing_prereqs
print_manual_failures

if [[ "${#MISSING_PACKAGES[@]}" -gt 0 || "${#FAILED_MANUAL_PREREQS[@]}" -gt 0 ]]; then
    exit 1
fi

echo
echo "All configured prerequisites are satisfied."
