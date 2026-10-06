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

set -euo pipefail

# regular/bold colors
BLACK="\033[0;30m"
BOLD_BLACK="\033[1;30m"

RED="\033[0;31m"
BOLD_RED="\033[1;31m"

GREEN="\033[0;32m"
BOLD_GREEN="\033[1;32m"

YELLOW="\033[0;33m"
BOLD_YELLOW="\033[1;33m"

BLUE="\033[0;34m"
BOLD_BLUE="\033[1;34m"

MAGENTA="\033[0;35m"
BOLD_MAGENTA="\033[1;35m"

CYAN="\033[0;36m"
BOLD_CYAN="\033[1;36m"

WHITE="\033[0;37m"
BOLD_WHITE="\033[1;37m"

# reset
RESET="\033[0m"

# other stuff
INVERSE="\033[7m"
UNDERLINE="\033[4m"

msg() { printf '%s→ %b\033[0m\n' "$(basename "$0")" "$*"; }

msg_begin() { printf '%s→ \033[7m\033[1;34m%b\033[0m\n' "$(basename "$0")" "$*"; }
msg_end() { printf '%s→ \033[7m\033[1;32m%b\033[0m\n' "$(basename "$0")" "$*"; }
msg_end_err() { printf '%s→ \033[7m\033[1;31m%b\033[0m\n' "$(basename "$0")" "$*"; }

need() {
    command -v "$1" > /dev/null 2>&1 || {
        echo "Missing tool: $1" >&2
        exit 127
    }
}
