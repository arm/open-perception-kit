#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################
# Quick-start host prerequisite package checks.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Usage:
  check-prerequisites.sh [--check-only] [-h|--help]

Checks the package prerequisites for the detected quick-start platform.
Missing packages are installed by default. Use --check-only to report missing
packages without installing them.

The platform package arrays are intentionally empty for now. Fill them in after
the minimal package sets are measured on clean target systems.
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

# Platform package lists. Keep these empty until the minimal sets are known.
PACKAGES_DEBIAN_13_X86=()
PACKAGES_UBUNTU_24_04_X86=(
    docker.io
    docker-compose-v2
)
PACKAGES_WSL=()
PACKAGES_WSL_DEBIAN_13=()
PACKAGES_WSL_UBUNTU_24_04=()
PACKAGES_RPI5=()
PACKAGES_RPI5_HAILO8=()
PACKAGES_RPI5_HAILO10=()
PACKAGES_MACOS=()
PACKAGES_LINUX_X86=()

PACKAGE_MANAGER=""
SELECTED_PACKAGE_ARRAYS=()
REQUIRED_PACKAGES=()
MISSING_PACKAGES=()

select_package_arrays() {
    case "$PEK_PLATFORM_ID" in
        macos)
            PACKAGE_MANAGER="brew"
            SELECTED_PACKAGE_ARRAYS=(PACKAGES_MACOS)
            ;;
        wsl)
            PACKAGE_MANAGER="apt"
            SELECTED_PACKAGE_ARRAYS=(PACKAGES_WSL)
            if [[ "$PEK_OS_ID" == "ubuntu" && "$PEK_OS_VERSION_CODENAME" == "noble" ]]; then
                SELECTED_PACKAGE_ARRAYS+=(PACKAGES_WSL_UBUNTU_24_04)
            elif [[ "$PEK_OS_ID" == "debian" && "$PEK_OS_VERSION_CODENAME" == "trixie" ]]; then
                SELECTED_PACKAGE_ARRAYS+=(PACKAGES_WSL_DEBIAN_13)
            fi
            ;;
        linux-x86_64)
            PACKAGE_MANAGER="apt"
            if [[ "$PEK_OS_ID" == "ubuntu" && "$PEK_OS_VERSION_CODENAME" == "noble" ]]; then
                SELECTED_PACKAGE_ARRAYS=(PACKAGES_UBUNTU_24_04_X86)
            elif [[ "$PEK_OS_ID" == "debian" && "$PEK_OS_VERSION_CODENAME" == "trixie" ]]; then
                SELECTED_PACKAGE_ARRAYS=(PACKAGES_DEBIAN_13_X86)
            else
                SELECTED_PACKAGE_ARRAYS=(PACKAGES_LINUX_X86)
            fi
            ;;
        rpi5 | rpi5-h10)
            PACKAGE_MANAGER="apt"
            SELECTED_PACKAGE_ARRAYS=(PACKAGES_RPI5)
            if [[ "$PEK_PLATFORM_ID" == "rpi5-h10" ]]; then
                SELECTED_PACKAGE_ARRAYS+=(PACKAGES_RPI5_HAILO10)
            elif [[ "$PEK_HAILO_ARCH" == "hailo8" || "$PEK_HAILO_ARCH" == "hailo8l" || "$PEK_HAILO_ARCH" == "hailo-unknown" ]]; then
                SELECTED_PACKAGE_ARRAYS+=(PACKAGES_RPI5_HAILO8)
            fi
            ;;
        *)
            echo "Error: no prerequisite package profile for platform '${PEK_PLATFORM_ID}'." >&2
            exit 1
            ;;
    esac
}

append_unique_package() {
    local pkg="$1"
    local existing

    for existing in "${REQUIRED_PACKAGES[@]}"; do
        if [[ "$existing" == "$pkg" ]]; then
            return
        fi
    done

    REQUIRED_PACKAGES+=("$pkg")
}

collect_required_packages() {
    local array_name
    local packages
    local pkg

    REQUIRED_PACKAGES=()

    for array_name in "${SELECTED_PACKAGE_ARRAYS[@]}"; do
        eval 'packages=("${'"$array_name"'[@]}")'
        for pkg in "${packages[@]}"; do
            append_unique_package "$pkg"
        done
    done
}

apt_package_installed() {
    dpkg-query -W -f='${Status}' "$1" 2> /dev/null | grep -q "install ok installed"
}

brew_package_installed() {
    brew list --formula "$1" > /dev/null 2>&1
}

package_installed() {
    case "$PACKAGE_MANAGER" in
        apt)
            apt_package_installed "$1"
            ;;
        brew)
            brew_package_installed "$1"
            ;;
        *)
            echo "Error: unsupported package manager '${PACKAGE_MANAGER}'." >&2
            exit 1
            ;;
    esac
}

collect_missing_packages() {
    local pkg

    MISSING_PACKAGES=()

    for pkg in "${REQUIRED_PACKAGES[@]}"; do
        if package_installed "$pkg"; then
            echo "OK: package installed: $pkg"
        else
            echo "MISSING: package not installed: $pkg"
            MISSING_PACKAGES+=("$pkg")
        fi
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

select_package_arrays
collect_required_packages

echo "Prerequisite checks:"
echo "  Platform: ${PEK_PLATFORM_NAME} (${PEK_PLATFORM_ID})"
echo "  Package manager: ${PACKAGE_MANAGER}"
echo "  Package arrays: ${SELECTED_PACKAGE_ARRAYS[*]}"

if [[ "${#REQUIRED_PACKAGES[@]}" -eq 0 ]]; then
    echo
    echo "No host packages are configured for this platform yet."
    exit 0
fi

echo
collect_missing_packages

if [[ "${#MISSING_PACKAGES[@]}" -eq 0 ]]; then
    echo
    echo "All configured prerequisite packages are installed."
    exit 0
fi

echo
echo "Missing packages:"
printf '  %s\n' "${MISSING_PACKAGES[@]}"

if [[ "$CHECK_ONLY" == "true" ]]; then
    exit 1
fi

echo
echo "Installing missing packages..."
install_missing_packages
