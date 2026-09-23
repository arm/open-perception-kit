#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
# Patches a generated Perception Cargo.toml for standalone crate
# publication: excludes vendored build inputs from the package, and
# points the pinned flatbuffers dependency at crates.io instead of the
# vendored path used by the in-tree build.
################################################################

set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <path-to-Cargo.toml>" >&2
    exit 2
fi

cargo_toml="$1"

sed -i \
    -e '/^\[package\]$/a exclude = ["vendor/**", ".cargo/**", "crates/**"]' \
    -e 's/^flatbuffers = "\(=[^"]*\)"$/flatbuffers = { version = "\1", registry = "crates-io" }/' \
    "$cargo_toml"

grep -Fqx 'exclude = ["vendor/**", ".cargo/**", "crates/**"]' "$cargo_toml"
grep -Eq '^flatbuffers = \{ version = "=[^"]+", registry = "crates-io" \}$' "$cargo_toml"
