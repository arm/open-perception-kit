#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

failures=0

check_cmd() {
    local name="$1"
    local cmd="$2"

    if command -v "$cmd" > /dev/null 2>&1; then
        echo "OK: $name found: $($cmd --version 2> /dev/null | head -n 1)"
    else
        echo "ERROR: $name is not installed or not on PATH"
        failures=$((failures + 1))
    fi
}

check_docker() {
    if command -v docker > /dev/null 2>&1; then
        echo "OK: Docker found: $(docker --version)"
    else
        echo "ERROR: Docker is not installed or not on PATH"
        failures=$((failures + 1))
        return
    fi

    if docker info > /dev/null 2>&1; then
        echo "OK: Docker daemon is running"
    else
        echo "ERROR: Docker is installed but the daemon is not reachable"
        failures=$((failures + 1))
    fi
}

check_docker_compose() {
    if docker compose version > /dev/null 2>&1; then
        echo "OK: Docker Compose found: $(docker compose version)"
    elif command -v docker-compose > /dev/null 2>&1; then
        echo "OK: legacy docker-compose found: $(docker-compose --version)"
    else
        echo "ERROR: Docker Compose is not installed"
        failures=$((failures + 1))
    fi
}

check_github_ssh_auth() {
    local output

    output="$(ssh -T -o BatchMode=yes git@github.com 2>&1 || true)"

    if echo "$output" | grep -qi "successfully authenticated"; then
        echo "OK: GitHub SSH authentication works"
    else
        echo "ERROR: GitHub SSH authentication failed"
        echo "$output"
        failures=$((failures + 1))
    fi
}

is_raspberry_pi() {
    grep -qi "raspberry pi" /proc/device-tree/model 2> /dev/null
}

check_rpi_supported_model() {
    if ! is_raspberry_pi; then
        return
    fi

    local model
    model="$(get_device_model)"

    if echo "$model" | grep -qi "raspberry pi 5"; then
        echo "OK: Supported device: $model"
    else
        echo "ERROR: Unsupported Raspberry Pi model"
        echo "       Detected: $model"
        echo "       Required: Raspberry Pi 5"
        failures=$((failures + 1))
    fi
}

is_raspberry_pi_5() {
    grep -qi "raspberry pi 5" /proc/device-tree/model 2> /dev/null
}

check_debian_trixie_on_rpi5() {
    if ! is_raspberry_pi_5; then
        return
    fi

    if [[ -r /etc/os-release ]]; then
        . /etc/os-release
    else
        echo "ERROR: Cannot read /etc/os-release"
        failures=$((failures + 1))
        return
    fi

    if [[ "${VERSION_CODENAME:-}" == "trixie" ]]; then
        echo "OK: Raspberry Pi 5 is running Debian Trixie"
    else
        echo "ERROR: Raspberry Pi 5 must run Debian Trixie"
        echo "       Detected: ${PRETTY_NAME:-unknown}"
        failures=$((failures + 1))
    fi
}

check_rpi_packages() {
    if ! is_raspberry_pi; then
        echo "OK: Not running on Raspberry Pi, skipping Raspberry Pi package checks"
        return
    fi

    local packages=(
        v4l-utils
        raspi-utils-core
        raspi-utils-dt
        rpicam-apps
        libcamera-dev
        libcamera-doc
        libcamera-tools
        gstreamer1.0-tools
        gstreamer1.0-plugins-good
        gstreamer1.0-plugins-bad
        gstreamer1.0-plugins-ugly
        gstreamer1.0-gl
        libgstreamer1.0-dev
        libgstreamer-plugins-base1.0-dev
        libgstreamer-plugins-bad1.0-dev
        gstreamer1.0-libcamera
        ffmpeg
        cmake
        libcairo2-dev
        libssl-dev
    )

    local missing=()

    for pkg in "${packages[@]}"; do
        if dpkg-query -W -f='${Status}' "$pkg" 2> /dev/null | grep -q "install ok installed"; then
            echo "OK: package installed: $pkg"
        else
            echo "ERROR: package missing: $pkg"
            missing+=("$pkg")
        fi
    done

    if [[ "${#missing[@]}" -gt 0 ]]; then
        failures=$((failures + 1))
        echo
        echo "Missing Raspberry Pi packages:"
        printf '  %s\n' "${missing[@]}"
    fi
}

check_demo_videos() {
    local repo_root
    repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

    if ! "${repo_root}/scripts/private/download-demo-videos.sh" --check; then
        echo "WARNING: demo videos are missing or corrupt; quick-start will restore them from the container image."
    fi
}

echo "Checking prerequisites..."
echo

check_docker
check_docker_compose
check_cmd "Git" git
check_cmd "VSCode" code
check_github_ssh_auth
check_rpi_supported_model
check_debian_trixie_on_rpi5
check_rpi_packages
check_demo_videos

echo
if [[ "$failures" -eq 0 ]]; then
    echo "All prerequisites are satisfied."
else
    echo "$failures prerequisite check(s) failed."
    exit 1
fi
