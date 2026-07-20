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

wheel_value() {
    python3 - "$MANIFEST" "$1" "$2" << 'PY'
import json
from pathlib import Path
import sys

manifest_path = Path(sys.argv[1])
architecture = sys.argv[2]
key = sys.argv[3]
try:
    document = json.loads(manifest_path.read_text(encoding="utf-8"))
    value = document["wheels"][architecture][key]
except (OSError, KeyError, TypeError, ValueError) as exc:
    raise SystemExit(f"Invalid modelfetch candidate manifest {manifest_path}: {exc}") from exc
if not isinstance(value, str):
    raise SystemExit(
        f"Invalid wheels.{architecture}.{key} in modelfetch candidate manifest {manifest_path}"
    )
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

if [[ -L "$CACHE_ROOT" ]]; then
    echo "Refusing unsafe modelfetch candidate cache root: ${CACHE_ROOT}" >&2
    exit 1
fi
CACHE_ROOT="$(python3 -c 'from pathlib import Path; import sys; print(Path(sys.argv[1]).resolve(strict=False))' "$CACHE_ROOT")"

repository="$(manifest_value repository)"
source_commit="$(manifest_value sourceCommit)"
run_id="$(manifest_value runId)"
artifact_name="$(manifest_value artifactName)"
expires_at="$(manifest_value expiresAt)"
wheel_filenames=(
    "$(wheel_value amd64 filename)"
    "$(wheel_value arm64 filename)"
)
wheel_sha256s=(
    "$(wheel_value amd64 sha256)"
    "$(wheel_value arm64 sha256)"
)
destinations=(
    "${CACHE_ROOT}/modelfetch-candidate-linux-amd64.whl"
    "${CACHE_ROOT}/modelfetch-candidate-linux-arm64.whl"
)

if ! [[ "$repository" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*/[A-Za-z0-9][A-Za-z0-9._-]*$ ]]; then
    echo "Invalid repository in ${MANIFEST}: ${repository}" >&2
    exit 1
fi
if ! [[ "$source_commit" =~ ^[0-9a-f]{40}$ ]]; then
    echo "Invalid sourceCommit in ${MANIFEST}: ${source_commit}" >&2
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
for index in 0 1; do
    wheel_filename="${wheel_filenames[$index]}"
    wheel_sha256="${wheel_sha256s[$index]}"
    if ! [[ "$wheel_filename" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*\.whl$ ]]; then
        echo "Invalid wheel filename in ${MANIFEST}: ${wheel_filename}" >&2
        exit 1
    fi
    case "$wheel_sha256" in
        *[!0-9a-f]* | "")
            echo "Invalid wheel sha256 in ${MANIFEST}" >&2
            exit 1
            ;;
    esac
    if [[ ${#wheel_sha256} -ne 64 ]]; then
        echo "Invalid wheel sha256 length in ${MANIFEST}" >&2
        exit 1
    fi
done

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

cache_is_current=true
for index in 0 1; do
    destination="${destinations[$index]}"
    if [[ -e "$destination" || -L "$destination" ]]; then
        if [[ ! -f "$destination" || -L "$destination" ]]; then
            echo "Refusing unsafe modelfetch candidate cache entry: ${destination}" >&2
            exit 1
        fi
        if [[ "$(file_sha256 "$destination")" != "${wheel_sha256s[$index]}" ]]; then
            cache_is_current=false
        fi
    else
        cache_is_current=false
    fi
done

if [[ "$cache_is_current" == true ]]; then
    printf '%s\n' "${destinations[@]}"
    exit 0
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
run_source_commit="$(
    gh run view "$run_id" \
        --repo "$repository" \
        --json headSha \
        --jq .headSha
)"
if [[ "$run_source_commit" != "$source_commit" ]]; then
    echo "Candidate run ${run_id} uses ${run_source_commit}, expected ${source_commit}." >&2
    exit 1
fi

mkdir -p "$CACHE_ROOT"
if [[ ! -d "$CACHE_ROOT" || -L "$CACHE_ROOT" ]]; then
    echo "Refusing unsafe modelfetch candidate cache root: ${CACHE_ROOT}" >&2
    exit 1
fi
temporary_dir="$(mktemp -d "${CACHE_ROOT}/.download.XXXXXX")"
cleanup() {
    rm -rf "$temporary_dir"
}
trap cleanup EXIT

gh run download "$run_id" \
    --repo "$repository" \
    --name "$artifact_name" \
    --dir "$temporary_dir"

for index in 0 1; do
    wheel_filename="${wheel_filenames[$index]}"
    wheel_sha256="${wheel_sha256s[$index]}"
    downloaded_wheel="${temporary_dir}/${wheel_filename}"
    if [[ ! -f "$downloaded_wheel" || -L "$downloaded_wheel" ]]; then
        echo "Artifact ${artifact_name} does not contain a regular ${wheel_filename}." >&2
        exit 1
    fi
    actual_sha256="$(file_sha256 "$downloaded_wheel")"
    if [[ "$actual_sha256" != "$wheel_sha256" ]]; then
        echo "Checksum mismatch for ${wheel_filename}: expected ${wheel_sha256}, got ${actual_sha256}." >&2
        exit 1
    fi
done

for index in 0 1; do
    mv "${temporary_dir}/${wheel_filenames[$index]}" "${destinations[$index]}"
    chmod 0644 "${destinations[$index]}"
done
printf '%s\n' "${destinations[@]}"
