#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: read-modelfetch-release-manifest.sh MANIFEST KEY" >&2
    exit 2
fi

manifest="$1"
requested_key="$2"

invalid_manifest() {
    echo "Invalid modelfetch release manifest ${manifest}." >&2
    exit 1
}

repository=""
tag=""
source_commit=""
amd64_filename=""
amd64_sha256=""
arm64_filename=""
arm64_sha256=""
repository_set=false
tag_set=false
source_commit_set=false
amd64_filename_set=false
amd64_sha256_set=false
arm64_filename_set=false
arm64_sha256_set=false

[[ -f "$manifest" ]] || invalid_manifest
while IFS= read -r line || [[ -n "$line" ]]; do
    [[ "$line" != *$'\r'* ]] || invalid_manifest
    if [[ -z "$line" ]]; then
        continue
    fi
    [[ "$line" == *=* ]] || invalid_manifest
    key="${line%%=*}"
    value="${line#*=}"
    value="${value% # pragma: allowlist secret}"
    case "$key" in
        repository)
            [[ "$repository_set" == false ]] || invalid_manifest
            repository="$value"
            repository_set=true
            ;;
        tag)
            [[ "$tag_set" == false ]] || invalid_manifest
            tag="$value"
            tag_set=true
            ;;
        source_commit)
            [[ "$source_commit_set" == false ]] || invalid_manifest
            source_commit="$value"
            source_commit_set=true
            ;;
        amd64_filename)
            [[ "$amd64_filename_set" == false ]] || invalid_manifest
            amd64_filename="$value"
            amd64_filename_set=true
            ;;
        amd64_sha256)
            [[ "$amd64_sha256_set" == false ]] || invalid_manifest
            amd64_sha256="$value"
            amd64_sha256_set=true
            ;;
        arm64_filename)
            [[ "$arm64_filename_set" == false ]] || invalid_manifest
            arm64_filename="$value"
            arm64_filename_set=true
            ;;
        arm64_sha256)
            [[ "$arm64_sha256_set" == false ]] || invalid_manifest
            arm64_sha256="$value"
            arm64_sha256_set=true
            ;;
        *)
            invalid_manifest
            ;;
    esac
done < "$manifest"

if [[ "$repository_set" != true ||
      "$tag_set" != true ||
      "$source_commit_set" != true ||
      "$amd64_filename_set" != true ||
      "$amd64_sha256_set" != true ||
      "$arm64_filename_set" != true ||
      "$arm64_sha256_set" != true ]]; then
    invalid_manifest
fi

[[ "$repository" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*/[A-Za-z0-9][A-Za-z0-9._-]*$ ]] ||
    invalid_manifest
[[ "$tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+([.-][A-Za-z0-9.]+)?$ ]] || invalid_manifest
[[ "$source_commit" =~ ^[0-9a-f]{40}$ ]] || invalid_manifest
for filename in "$amd64_filename" "$arm64_filename"; do
    [[ "$filename" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*\.tar\.gz$ ]] || invalid_manifest
done
for checksum in "$amd64_sha256" "$arm64_sha256"; do
    [[ "$checksum" =~ ^[0-9a-f]{64}$ ]] || invalid_manifest
done

case "$requested_key" in
    repository)
        printf '%s\n' "$repository"
        ;;
    tag)
        printf '%s\n' "$tag"
        ;;
    source_commit)
        printf '%s\n' "$source_commit"
        ;;
    amd64_filename)
        printf '%s\n' "$amd64_filename"
        ;;
    amd64_sha256)
        printf '%s\n' "$amd64_sha256"
        ;;
    arm64_filename)
        printf '%s\n' "$arm64_filename"
        ;;
    arm64_sha256)
        printf '%s\n' "$arm64_sha256"
        ;;
    *)
        echo "Unknown modelfetch release manifest key: ${requested_key}" >&2
        exit 2
        ;;
esac
