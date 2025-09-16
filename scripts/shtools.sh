
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

msg_begin() { printf '%s→ \033[1;34m%b\033[0m\n' "$(basename "$0")" "$*"; }
msg_end() { printf '%s→ \033[1;32m%b\033[0m\n' "$(basename "$0")" "$*"; }
msg_endp() { printf '%s→ \033[1;31m%b\033[0m\n' "$(basename "$0")" "$*"; }

need() { command -v "$1" >/dev/null 2>&1 || { echo "Missing tool: $1" >&2; exit 127; }; }

