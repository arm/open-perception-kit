#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################
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
	cat <<'EOF'
Usage:
  detect-environment.sh [--shell] [-h|--help]

Detects the quick-start host environment.

Supported host classes:
  rpi5         Raspberry Pi 5 running Linux
  linux-x86_64 Generic x86_64 Linux host
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
		tr -d '\0' <"$path"
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
		text+="$(</proc/sys/kernel/osrelease)"
	fi
	if [[ -r /proc/version ]]; then
		text+=" $(</proc/version)"
	fi

	[[ "$text" =~ [Mm]icrosoft|WSL ]]
}

detect_rpi_model() {
	read_file_or_empty /proc/device-tree/model
}

detect_hailo_arch() {
	local output=""

	if command -v hailortcli >/dev/null 2>&1; then
		output="$(hailortcli fw-control identify 2>/dev/null || true)"
		if grep -qi "HAILO10" <<<"$output"; then
			printf "hailo10"
			return
		fi
		if grep -qi "HAILO8L" <<<"$output"; then
			printf "hailo8l"
			return
		fi
		if grep -qi "HAILO8" <<<"$output"; then
			printf "hailo8"
			return
		fi
	fi

	if ls /dev/hailo* >/dev/null 2>&1; then
		printf "hailo-unknown"
		return
	fi

	printf "none"
}

detect_environment() {
	PEK_UNAME_S="$(uname -s)"
	PEK_UNAME_M="$(uname -m)"
	PEK_PRETTY_OS="$(detect_os_release_field PRETTY_NAME)"
	PEK_OS_ID="$(detect_os_release_field ID)"
	PEK_OS_VERSION_ID="$(detect_os_release_field VERSION_ID)"
	PEK_OS_VERSION_CODENAME="$(detect_os_release_field VERSION_CODENAME)"
	PEK_RPI_MODEL="$(detect_rpi_model)"
	PEK_HAILO_ARCH="$(detect_hailo_arch)"
	PEK_IN_CONTAINER="false"

	if [[ -f /.dockerenv ]] || grep -qaE '/docker/|/containers/' /proc/1/cgroup 2>/dev/null; then
		PEK_IN_CONTAINER="true"
	fi

	PEK_PLATFORM_ID="unsupported"
	PEK_PLATFORM_NAME="Unsupported platform"
	PEK_CONTAINER_SERVICE=""
	PEK_CONTAINER_NAME=""
	PEK_SUPPORTED="false"
	PEK_UNSUPPORTED_REASON=""

	case "$PEK_UNAME_S" in
	Darwin)
		PEK_PLATFORM_ID="macos"
		PEK_PLATFORM_NAME="macOS"
		PEK_CONTAINER_SERVICE="pek-dev-base"
		PEK_CONTAINER_NAME="perception-experience-kit"
		PEK_SUPPORTED="true"
		;;
	Linux)
		if is_wsl; then
			PEK_PLATFORM_ID="wsl"
			PEK_PLATFORM_NAME="Windows Subsystem for Linux"
			PEK_CONTAINER_SERVICE="pek-dev-base"
			PEK_CONTAINER_NAME="perception-experience-kit"
			PEK_SUPPORTED="true"
		elif grep -qi "raspberry pi 5" <<<"$PEK_RPI_MODEL"; then
			if [[ "$PEK_HAILO_ARCH" == "hailo10" ]]; then
				PEK_PLATFORM_ID="rpi5-h10"
				PEK_PLATFORM_NAME="Raspberry Pi 5 with Hailo 10"
				PEK_CONTAINER_SERVICE="pek-dev-rpi5-h10"
				PEK_CONTAINER_NAME="perception-experience-kit-rpi5-h10"
			else
				PEK_PLATFORM_ID="rpi5"
				PEK_PLATFORM_NAME="Raspberry Pi 5"
				PEK_CONTAINER_SERVICE="pek-dev-rpi5-h8"
				PEK_CONTAINER_NAME="perception-experience-kit-rpi5"
			fi
			PEK_SUPPORTED="true"
		elif [[ "$PEK_UNAME_M" == "x86_64" || "$PEK_UNAME_M" == "amd64" ]]; then
			PEK_PLATFORM_ID="linux-x86_64"
			PEK_PLATFORM_NAME="Linux x86_64"
			PEK_CONTAINER_SERVICE="pek-dev-base"
			PEK_CONTAINER_NAME="perception-experience-kit"
			PEK_SUPPORTED="true"
		else
			PEK_UNSUPPORTED_REASON="Linux host is not Raspberry Pi 5 or x86_64."
		fi
		;;
	*)
		PEK_UNSUPPORTED_REASON="Kernel '$PEK_UNAME_S' is not supported by the quick-start flow."
		;;
	esac
}

print_shell() {
	local names=(
		PEK_SUPPORTED
		PEK_PLATFORM_ID
		PEK_PLATFORM_NAME
		PEK_CONTAINER_SERVICE
		PEK_CONTAINER_NAME
		PEK_UNAME_S
		PEK_UNAME_M
		PEK_PRETTY_OS
		PEK_OS_ID
		PEK_OS_VERSION_ID
		PEK_OS_VERSION_CODENAME
		PEK_RPI_MODEL
		PEK_HAILO_ARCH
		PEK_IN_CONTAINER
		PEK_UNSUPPORTED_REASON
	)
	local name

	for name in "${names[@]}"; do
		printf "%s=%s\n" "$name" "$(shell_escape "${!name}")"
	done
}

print_human() {
	echo "Detected environment:"
	echo "  Platform: ${PEK_PLATFORM_NAME} (${PEK_PLATFORM_ID})"
	echo "  Kernel:   ${PEK_UNAME_S}"
	echo "  Machine:  ${PEK_UNAME_M}"

	if [[ -n "$PEK_PRETTY_OS" ]]; then
		echo "  OS:       ${PEK_PRETTY_OS}"
	fi
	if [[ -n "$PEK_RPI_MODEL" ]]; then
		echo "  Device:   ${PEK_RPI_MODEL}"
	fi
	if [[ "$PEK_HAILO_ARCH" != "none" ]]; then
		echo "  Hailo:    ${PEK_HAILO_ARCH}"
	fi
	echo "  Container shell: ${PEK_IN_CONTAINER}"

	if [[ "$PEK_SUPPORTED" == "true" ]]; then
		echo
		echo "Quick-start target:"
		echo "  Compose service: ${PEK_CONTAINER_SERVICE}"
		echo "  Container name:  ${PEK_CONTAINER_NAME}"
	else
		echo
		echo "Unsupported platform."
		if [[ -n "$PEK_UNSUPPORTED_REASON" ]]; then
			echo "Reason: ${PEK_UNSUPPORTED_REASON}"
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

	if [[ "$PEK_SUPPORTED" != "true" ]]; then
		exit 1
	fi
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
	main "$@"
fi
