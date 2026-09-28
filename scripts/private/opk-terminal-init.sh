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

# Always unset the welcome flag at the start so it is per-terminal, not global.
unset OPK_TERMINAL_WELCOME_SHOWN

# Load the user's regular interactive shell setup first.
export OPK_TERMINAL_INIT_ACTIVE=1

if [[ -z "${OPK_TERMINAL_INIT_SKIP_BASHRC:-}" ]] && [[ -f "$HOME/.bashrc" ]]; then
    # shellcheck disable=SC1090
    source "$HOME/.bashrc"
fi

unset OPK_TERMINAL_INIT_ACTIVE

# Show the welcome banner once per terminal session.
if [[ -z "${OPK_TERMINAL_WELCOME_SHOWN:-}" ]]; then
    export OPK_TERMINAL_WELCOME_SHOWN=1

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
    frame_line "OPK repo ready"
    frame_mid
    frame_line "Build cmd       ./scripts/build.sh debug"
    frame_line "Build task      00 Build Project"
    frame_line "Launch cmd      ./tools/opk-menu -l"
    frame_line "Launch task     00 Run project with latest pipeline"
    frame_line "Docs gen        ./scripts/gen-doc.sh"
    frame_line "Docs serve      ./scripts/serve-docs.sh"
    frame_sep
    frame_line "Web UI          http://${primary_host}:9999"
    frame_line "Docs            http://${primary_host}:8080/index.html"
    frame_sep
    frame_line "Ref             ./docs/public/index.md"
    frame_bottom
    echo
fi
