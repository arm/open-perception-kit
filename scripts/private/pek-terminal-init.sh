#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

# Always unset the welcome flag at the start so it is per-terminal, not global.
unset PEK_TERMINAL_WELCOME_SHOWN

# Load the user's regular interactive shell setup first.
export PEK_TERMINAL_INIT_ACTIVE=1

if [[ -z "${PEK_TERMINAL_INIT_SKIP_BASHRC:-}" ]] && [[ -f "$HOME/.bashrc" ]]; then
	# shellcheck disable=SC1090
	source "$HOME/.bashrc"
fi

unset PEK_TERMINAL_INIT_ACTIVE

# Show the welcome banner once per terminal session.
if [[ -z "${PEK_TERMINAL_WELCOME_SHOWN:-}" ]]; then
	export PEK_TERMINAL_WELCOME_SHOWN=1

	frame_inner_width=75

	frame_line() {
		printf '║ %-*s ║\n' "$frame_inner_width" "$1"
	}

	frame_sep() {
		printf '╟'
		printf '─%.0s' $(seq 1 $((frame_inner_width + 2)))
		printf '╢\n'
	}

	frame_top() {
		printf '╔'
		printf '═%.0s' $(seq 1 $((frame_inner_width + 2)))
		printf '╗\n'
	}

	frame_mid() {
		printf '╠'
		printf '═%.0s' $(seq 1 $((frame_inner_width + 2)))
		printf '╣\n'
	}

	frame_bottom() {
		printf '╚'
		printf '═%.0s' $(seq 1 $((frame_inner_width + 2)))
		printf '╝\n'
	}

	primary_host="localhost"
	if [[ "$(uname -m)" == "aarch64" ]]; then
		primary_host="raspberrypi.local"
	fi

	echo
	frame_top
	frame_line "PEK repo ready"
	frame_mid
	frame_line "Build cmd       ./scripts/build-elements.sh debug"
	frame_line "Build task      00 Build Project"
	frame_line "Launch cmd      /work/tools/pek-menu -l"
	frame_line "Launch task     00 Run project with latest pipeline"
	frame_line "Docs gen        ./scripts/gen-doc.sh"
	frame_line "Docs serve      ./scripts/serve-docs.sh"
	frame_sep
	frame_line "Web UI          http://${primary_host}:9999"
	frame_line "Docs            http://${primary_host}:8080/index.html"
	frame_sep
	frame_line "Ref             /work/docs/public/index.md"
	frame_bottom
	echo
fi
