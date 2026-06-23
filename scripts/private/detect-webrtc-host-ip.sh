#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################
# Prints the host/LAN IP address that other devices should use for WebRTC.
#
# This runs on the Docker host shell. On Windows, that shell is expected to be
# WSL; the selected address is still the LAN-facing address peers can reach.
################################################################

set -euo pipefail

usage() {
    cat << 'EOF'
Usage:
  detect-webrtc-host-ip.sh [-h|--help]

Prints the host IP address to advertise to local WebRTC peers.

Override detection by setting WEBRTC_HOST_IP before starting Docker Compose.
EOF
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

first_private_ipv4_from_lines() {
    awk '
        $1 ~ /^([0-9]{1,3}\.){3}[0-9]{1,3}$/ {
            if ($1 ~ /^10\./ || $1 ~ /^192\.168\./ || $1 ~ /^172\.(1[6-9]|2[0-9]|3[0-1])\./) {
                print $1
                exit
            }
        }
    '
}

default_route_ipv4_linux() {
    local ip=""

    if command -v ip > /dev/null 2>&1; then
        ip="$(ip -4 route get 1.1.1.1 2> /dev/null | awk '
            {
                for (i = 1; i <= NF; i++) {
                    if ($i == "src" && (i + 1) <= NF) {
                        print $(i + 1)
                        exit
                    }
                }
            }
        ' || true)"
    fi

    printf "%s" "$ip"
}

default_route_ipv4_macos() {
    local iface=""

    if ! command -v route > /dev/null 2>&1 || ! command -v ipconfig > /dev/null 2>&1; then
        return
    fi

    iface="$(route -n get default 2> /dev/null | awk '/interface:/{print $2; exit}')"
    if [[ -n "$iface" ]]; then
        ipconfig getifaddr "$iface" 2> /dev/null || true
    fi
}

wsl_windows_lan_ipv4() {
    if ! command -v powershell.exe > /dev/null 2>&1; then
        return
    fi

    powershell.exe -NoProfile -Command \
        "Get-NetIPConfiguration | Where-Object { \$_.IPv4DefaultGateway -ne \$null -and \$_.NetAdapter.Status -eq 'Up' } | ForEach-Object { \$_.IPv4Address.IPAddress } | Select-Object -First 1" \
        2> /dev/null | tr -d '\r' | first_private_ipv4_from_lines
}

fallback_ipv4() {
    local ip=""

    if command -v hostname > /dev/null 2>&1; then
        ip="$(hostname -I 2> /dev/null | tr ' ' '\n' | first_private_ipv4_from_lines || true)"
    fi

    if [[ -z "$ip" ]] && command -v ifconfig > /dev/null 2>&1; then
        ip="$(ifconfig 2> /dev/null | awk '/inet /{print $2}' | first_private_ipv4_from_lines || true)"
    fi

    if [[ -z "$ip" ]] && command -v getent > /dev/null 2>&1; then
        ip="$(getent ahostsv4 "$(hostname)" 2> /dev/null | awk '{print $1}' | first_private_ipv4_from_lines || true)"
    fi

    printf "%s" "$ip"
}

detect_webrtc_host_ip() {
    local ip=""

    if [[ -n "${WEBRTC_HOST_IP:-}" ]]; then
        printf "%s\n" "$WEBRTC_HOST_IP"
        return
    fi

    case "$(uname -s)" in
        Darwin)
            ip="$(default_route_ipv4_macos)"
            ;;
        Linux)
            if is_wsl; then
                ip="$(wsl_windows_lan_ipv4)"
            fi
            if [[ -z "$ip" ]]; then
                ip="$(default_route_ipv4_linux)"
            fi
            ;;
    esac

    if [[ -z "$ip" ]]; then
        ip="$(fallback_ipv4)"
    fi

    if [[ -z "$ip" ]]; then
        echo "Error: could not detect WEBRTC_HOST_IP. Set it explicitly, for example:" >&2
        echo "  export WEBRTC_HOST_IP=192.168.2.192" >&2
        exit 1
    fi

    printf "%s\n" "$ip"
}

case "${1:-}" in
    -h | --help)
        usage
        exit 0
        ;;
    "")
        detect_webrtc_host_ip
        ;;
    *)
        echo "Error: unknown argument '${1}'" >&2
        echo >&2
        usage >&2
        exit 2
        ;;
esac
