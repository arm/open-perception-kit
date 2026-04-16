#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat << 'EOF'
Usage:
  run-console-enter [-h]

Enters the console based development environment.

Notes:
  Requires the 'amp-dev-rich' container to be running.rich
  If it isn't running, start it with: ./scripts/private/run-console
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
elif [[ "${1:-}" != "" ]]; then
    echo "Error: unknown argument '${1}'" >&2
    echo >&2
    usage >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

DEV_ENV_FILE="devices.env"
DC_RICH="rich"

cd "${REPO_ROOT}"

# Check if container is running
if ! docker inspect -f '{{.State.Running}}' amp-dev-rich > /dev/null 2>&1; then
    echo "Error: container 'amp-dev-rich' is not running." >&2
    echo "Please start it first by running: ./scripts/private/run-console" >&2
    exit 1
fi

./scripts/private/dev-init.sh amp-dev-rich "$DC_RICH" "$DEV_ENV_FILE"

docker exec -it -u devgoblin --env-file "${DEV_ENV_FILE}" -e TERM="$TERM" amp-dev-rich zsh
