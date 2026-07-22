#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
MANIFEST="${MODELFETCH_RELEASE_MANIFEST:-${SCRIPT_DIR}/modelfetch-release.json}"
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
    raise SystemExit(f"Invalid modelfetch release manifest {manifest_path}: {exc}") from exc
if not isinstance(value, str):
    raise SystemExit(f"Invalid {key} in modelfetch release manifest {manifest_path}")
print(value)
PY
}

sdk_value() {
    python3 - "$MANIFEST" "$1" "$2" << 'PY'
import json
from pathlib import Path
import sys

manifest_path = Path(sys.argv[1])
architecture = sys.argv[2]
key = sys.argv[3]
try:
    document = json.loads(manifest_path.read_text(encoding="utf-8"))
    value = document["sdks"][architecture][key]
except (OSError, KeyError, TypeError, ValueError) as exc:
    raise SystemExit(f"Invalid modelfetch release manifest {manifest_path}: {exc}") from exc
if not isinstance(value, str):
    raise SystemExit(
        f"Invalid sdks.{architecture}.{key} in modelfetch release manifest {manifest_path}"
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
    echo "Python 3 is required to validate the pinned modelfetch release." >&2
    exit 1
fi

if [[ -L "$CACHE_ROOT" ]]; then
    echo "Refusing unsafe modelfetch release cache root: ${CACHE_ROOT}" >&2
    exit 1
fi
CACHE_ROOT="$(python3 -c 'from pathlib import Path; import sys; print(Path(sys.argv[1]).resolve(strict=False))' "$CACHE_ROOT")"

repository="$(manifest_value repository)"
tag="$(manifest_value tag)"
source_commit="$(manifest_value sourceCommit)"
sdk_filenames=(
    "$(sdk_value amd64 filename)"
    "$(sdk_value arm64 filename)"
)
sdk_sha256s=(
    "$(sdk_value amd64 sha256)"
    "$(sdk_value arm64 sha256)"
)
destinations=(
    "${CACHE_ROOT}/modelfetch-release-linux-amd64.tar.gz"
    "${CACHE_ROOT}/modelfetch-release-linux-arm64.tar.gz"
)

if ! [[ "$repository" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*/[A-Za-z0-9][A-Za-z0-9._-]*$ ]]; then
    echo "Invalid repository in ${MANIFEST}: ${repository}" >&2
    exit 1
fi
if ! [[ "$tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+([.-][A-Za-z0-9.]+)?$ ]]; then
    echo "Invalid release tag in ${MANIFEST}: ${tag}" >&2
    exit 1
fi
if ! [[ "$source_commit" =~ ^[0-9a-f]{40}$ ]]; then
    echo "Invalid sourceCommit in ${MANIFEST}: ${source_commit}" >&2
    exit 1
fi
for index in 0 1; do
    sdk_filename="${sdk_filenames[$index]}"
    sdk_sha256="${sdk_sha256s[$index]}"
    if ! [[ "$sdk_filename" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*\.tar\.gz$ ]]; then
        echo "Invalid SDK filename in ${MANIFEST}: ${sdk_filename}" >&2
        exit 1
    fi
    case "$sdk_sha256" in
        *[!0-9a-f]* | "")
            echo "Invalid SDK sha256 in ${MANIFEST}" >&2
            exit 1
            ;;
    esac
    if [[ ${#sdk_sha256} -ne 64 ]]; then
        echo "Invalid SDK sha256 length in ${MANIFEST}" >&2
        exit 1
    fi
done

cache_is_current=true
for index in 0 1; do
    destination="${destinations[$index]}"
    if [[ -e "$destination" || -L "$destination" ]]; then
        if [[ ! -f "$destination" || -L "$destination" ]]; then
            echo "Refusing unsafe modelfetch release cache entry: ${destination}" >&2
            exit 1
        fi
        if [[ "$(file_sha256 "$destination")" != "${sdk_sha256s[$index]}" ]]; then
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
    echo "GitHub CLI is required to acquire the pinned modelfetch release." >&2
    exit 1
fi
if ! gh auth status --hostname github.com > /dev/null 2>&1; then
    echo "GitHub CLI must be authenticated with release read access to ${repository}." >&2
    echo "Run 'gh auth login --hostname github.com' or provide GH_TOKEN, then retry." >&2
    exit 1
fi

read -r release_tag is_draft is_prerelease <<< "$(
    gh release view "$tag" \
        --repo "$repository" \
        --json tagName,isDraft,isPrerelease \
        --jq '[.tagName, .isDraft, .isPrerelease] | @tsv'
)"
if [[ "$release_tag" != "$tag" || "$is_draft" != "false" || "$is_prerelease" != "false" ]]; then
    echo "Release ${tag} in ${repository} is missing, draft, or prerelease." >&2
    exit 1
fi

release_source_commit="$(
    gh api "repos/${repository}/commits/${tag}" --jq .sha
)"
if [[ "$release_source_commit" != "$source_commit" ]]; then
    echo "Release ${tag} uses ${release_source_commit}, expected ${source_commit}." >&2
    exit 1
fi

mkdir -p "$CACHE_ROOT"
if [[ ! -d "$CACHE_ROOT" || -L "$CACHE_ROOT" ]]; then
    echo "Refusing unsafe modelfetch release cache root: ${CACHE_ROOT}" >&2
    exit 1
fi
temporary_dir="$(mktemp -d "${CACHE_ROOT}/.download.XXXXXX")"
cleanup() {
    rm -rf "$temporary_dir"
}
trap cleanup EXIT

gh release download "$tag" \
    --repo "$repository" \
    --pattern "${sdk_filenames[0]}" \
    --pattern "${sdk_filenames[1]}" \
    --dir "$temporary_dir"

for index in 0 1; do
    sdk_filename="${sdk_filenames[$index]}"
    sdk_sha256="${sdk_sha256s[$index]}"
    downloaded_sdk="${temporary_dir}/${sdk_filename}"
    if [[ ! -f "$downloaded_sdk" || -L "$downloaded_sdk" ]]; then
        echo "Release ${tag} does not contain a regular ${sdk_filename}." >&2
        exit 1
    fi
    actual_sha256="$(file_sha256 "$downloaded_sdk")"
    if [[ "$actual_sha256" != "$sdk_sha256" ]]; then
        echo "Checksum mismatch for ${sdk_filename}: expected ${sdk_sha256}, got ${actual_sha256}." >&2
        exit 1
    fi
done

for index in 0 1; do
    mv "${temporary_dir}/${sdk_filenames[$index]}" "${destinations[$index]}"
    chmod 0644 "${destinations[$index]}"
done
printf '%s\n' "${destinations[@]}"
