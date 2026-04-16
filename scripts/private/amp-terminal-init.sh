#!/usr/bin/env bash

# Load the user's regular interactive shell setup first.
export AMP_TERMINAL_INIT_ACTIVE=1

if [[ -z "${AMP_TERMINAL_INIT_SKIP_BASHRC:-}" ]] && [[ -f "$HOME/.bashrc" ]]; then
    # shellcheck disable=SC1090
    source "$HOME/.bashrc"
fi

unset AMP_TERMINAL_INIT_ACTIVE

# Show the welcome banner once per terminal session.
if [[ -z "${AMP_TERMINAL_WELCOME_SHOWN:-}" ]]; then
    export AMP_TERMINAL_WELCOME_SHOWN=1

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
    frame_line "AMP repo ready"
    frame_mid
    frame_line "Build cmd  ./scripts/build-elements.sh debug"
    frame_line "Build task 00 Build Project"
    frame_line "Launch cmd /work/tools/amp-menu -l"
    frame_line "Launch task 99 Launch Without Debug"
    frame_line "Docs gen   ./scripts/gen-doc.sh"
    frame_line "Docs serve ./scripts/serve-docs.sh"
    frame_sep
    frame_line "Web UI     http://${primary_host}:9999"
    frame_line "Docs       http://${primary_host}:8080/index.html"
    frame_sep
    frame_line "Ref        /work/docs/content/corespec/how-to.md"
    frame_bottom
    echo
fi
