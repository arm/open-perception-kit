#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
MANIFEST="${MODELFETCH_RELEASE_MANIFEST:-${SCRIPT_DIR}/modelfetch-release.manifest}"
CACHE_ROOT="${MODELFETCH_CACHE_ROOT:-${REPO_ROOT}/.cache/modelfetch}"
MANIFEST_READER="${SCRIPT_DIR}/read-modelfetch-release-manifest.sh"

file_sha256() {
    local output
    if command -v sha256sum > /dev/null 2>&1; then
        output="$(sha256sum "$1")"
    elif command -v shasum > /dev/null 2>&1; then
        output="$(shasum -a 256 "$1")"
    else
        echo "sha256sum or shasum is required to validate the pinned modelfetch release." >&2
        return 1
    fi
    printf '%s\n' "${output%% *}"
}

repository="$("$MANIFEST_READER" "$MANIFEST" repository)"
tag="$("$MANIFEST_READER" "$MANIFEST" tag)"
source_commit="$("$MANIFEST_READER" "$MANIFEST" source_commit)"
amd64_filename="$("$MANIFEST_READER" "$MANIFEST" amd64_filename)"
amd64_sha256="$("$MANIFEST_READER" "$MANIFEST" amd64_sha256)"
arm64_filename="$("$MANIFEST_READER" "$MANIFEST" arm64_filename)"
arm64_sha256="$("$MANIFEST_READER" "$MANIFEST" arm64_sha256)"

if [[ -L "$CACHE_ROOT" ]]; then
    echo "Refusing unsafe modelfetch release cache root: ${CACHE_ROOT}" >&2
    exit 1
fi
sdk_filenames=(
    "$amd64_filename"
    "$arm64_filename"
)
sdk_sha256s=(
    "$amd64_sha256"
    "$arm64_sha256"
)

mkdir -p "$CACHE_ROOT"
if [[ ! -d "$CACHE_ROOT" || -L "$CACHE_ROOT" ]]; then
    echo "Refusing unsafe modelfetch release cache root: ${CACHE_ROOT}" >&2
    exit 1
fi
CACHE_ROOT="$(cd "$CACHE_ROOT" && pwd -P)"
destinations=(
    "${CACHE_ROOT}/modelfetch-release-linux-amd64.tar.gz"
    "${CACHE_ROOT}/modelfetch-release-linux-arm64.tar.gz"
)

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

if ! release_details="$(
    gh release view "$tag" \
        --repo "$repository" \
        --json tagName,isDraft,isPrerelease \
        --jq '[.tagName, .isDraft, .isPrerelease] | @tsv'
)"; then
    echo "GitHub CLI must be authenticated with release read access to ${repository}." >&2
    echo "Run 'gh auth login --hostname github.com' or provide GH_TOKEN, then retry." >&2
    exit 1
fi
read -r release_tag is_draft is_prerelease <<< "$release_details"
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
