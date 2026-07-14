#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
MANIFEST="${MODELFETCH_CANDIDATE_MANIFEST:-${SCRIPT_DIR}/modelfetch-candidate.json}"
CACHE_ROOT="${MODELFETCH_CACHE_ROOT:-${REPO_ROOT}/.cache/modelfetch}"

manifest_value() {
    python3 - "$MANIFEST" "$1" << 'PY'
import json
from pathlib import Path
import sys

manifest_path = Path(sys.argv[1])
key = sys.argv[2]
try:
    document = json.loads(manifest_path.read_text(encoding="utf-8"))
    value = document[key]
except (OSError, KeyError, TypeError, ValueError) as exc:
    raise SystemExit(f"Invalid modelfetch candidate manifest {manifest_path}: {exc}") from exc
if not isinstance(value, (str, int)) or isinstance(value, bool):
    raise SystemExit(f"Invalid {key} in modelfetch candidate manifest {manifest_path}")
print(value)
PY
}

file_sha256() {
    python3 - "$1" << 'PY'
from hashlib import sha256
from pathlib import Path
import sys

digest = sha256()
with Path(sys.argv[1]).open("rb") as handle:
    for chunk in iter(lambda: handle.read(1024 * 1024), b""):
        digest.update(chunk)
print(digest.hexdigest())
PY
}

if ! command -v python3 > /dev/null 2>&1; then
    echo "Python 3 is required to validate the pinned modelfetch candidate." >&2
    exit 1
fi

repository="$(manifest_value repository)"
run_id="$(manifest_value runId)"
artifact_name="$(manifest_value artifactName)"
wheel_filename="$(manifest_value wheelFilename)"
wheel_sha256="$(manifest_value wheelSha256)"
expires_at="$(manifest_value expiresAt)"

if ! [[ "$repository" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*/[A-Za-z0-9][A-Za-z0-9._-]*$ ]]; then
    echo "Invalid repository in ${MANIFEST}: ${repository}" >&2
    exit 1
fi
case "$run_id" in
    *[!0-9]* | "")
        echo "Invalid runId in ${MANIFEST}: ${run_id}" >&2
        exit 1
        ;;
esac
if ! [[ "$artifact_name" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]]; then
    echo "Invalid artifactName in ${MANIFEST}: ${artifact_name}" >&2
    exit 1
fi
if ! [[ "$wheel_filename" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*\.whl$ ]]; then
    echo "Invalid wheelFilename in ${MANIFEST}: ${wheel_filename}" >&2
    exit 1
fi
case "$wheel_sha256" in
    *[!0-9a-f]* | "")
        echo "Invalid wheelSha256 in ${MANIFEST}" >&2
        exit 1
        ;;
esac
if [[ ${#wheel_sha256} -ne 64 ]]; then
    echo "Invalid wheelSha256 length in ${MANIFEST}" >&2
    exit 1
fi

python3 - "$expires_at" << 'PY'
from datetime import datetime, timezone
import sys

try:
    expires_at = datetime.fromisoformat(sys.argv[1].replace("Z", "+00:00"))
except ValueError as exc:
    raise SystemExit(f"Invalid modelfetch candidate expiry: {sys.argv[1]}") from exc
if expires_at.tzinfo is None or expires_at <= datetime.now(timezone.utc):
    raise SystemExit(
        f"The pinned modelfetch candidate expired at {sys.argv[1]}; refresh it or use a release."
    )
PY

destination="${CACHE_ROOT}/modelfetch-candidate.whl"
if [[ -e "$destination" || -L "$destination" ]]; then
    if [[ ! -f "$destination" || -L "$destination" ]]; then
        echo "Refusing unsafe modelfetch candidate cache entry: ${destination}" >&2
        exit 1
    fi
    if [[ "$(file_sha256 "$destination")" == "$wheel_sha256" ]]; then
        printf '%s\n' "$destination"
        exit 0
    fi
fi

if ! command -v gh > /dev/null 2>&1; then
    echo "GitHub CLI is required to acquire the pinned modelfetch candidate." >&2
    exit 1
fi
if ! gh auth status --hostname github.com > /dev/null 2>&1; then
    echo "GitHub CLI must be authenticated with Actions read access to ${repository}." >&2
    echo "Run 'gh auth login --hostname github.com' or provide GH_TOKEN, then retry." >&2
    exit 1
fi

mkdir -p "$CACHE_ROOT"
temporary_dir="$(mktemp -d "${CACHE_ROOT}/.download.XXXXXX")"
cleanup() {
    rm -rf "$temporary_dir"
}
trap cleanup EXIT

gh run download "$run_id" \
    --repo "$repository" \
    --name "$artifact_name" \
    --dir "$temporary_dir"

downloaded_wheel="${temporary_dir}/${wheel_filename}"
if [[ ! -f "$downloaded_wheel" ]]; then
    echo "Artifact ${artifact_name} does not contain ${wheel_filename}." >&2
    exit 1
fi
actual_sha256="$(file_sha256 "$downloaded_wheel")"
if [[ "$actual_sha256" != "$wheel_sha256" ]]; then
    echo "Checksum mismatch for ${wheel_filename}: expected ${wheel_sha256}, got ${actual_sha256}." >&2
    exit 1
fi

mv "$downloaded_wheel" "$destination"
chmod 0644 "$destination"
printf '%s\n' "$destination"
