#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

if [[ $# -ne 4 ]]; then
    echo "Usage: BuildDocsPackage.sh BUILD_ID OUTPUT_DIR HTML_DIR DOXYGEN_DIR" >&2
    exit 2
fi

BuildId="$1"
OutputDir="$(realpath -m "$2")"
HtmlDir="$(realpath "$3")"
DoxygenDir="$(realpath "$4")"
[[ -f "$HtmlDir/index.html" && -f "$DoxygenDir/index.html" ]] || {
    echo "Generated offline or Doxygen documentation is missing" >&2
    exit 1
}

TemporaryRoot="$(mktemp -d)"
trap 'rm -rf "$TemporaryRoot"' EXIT
PackageName="pek-docs-$BuildId"
mkdir -p "$TemporaryRoot/$PackageName/html" "$TemporaryRoot/$PackageName/doxygen" "$OutputDir"
cp -a "$HtmlDir/." "$TemporaryRoot/$PackageName/html/"
cp -a "$DoxygenDir/." "$TemporaryRoot/$PackageName/doxygen/"

Archive="$OutputDir/$PackageName.tar.gz"
tar -C "$TemporaryRoot" -czf "$Archive" "$PackageName"
sha256sum "$Archive"
