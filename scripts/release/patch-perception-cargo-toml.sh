#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
# Patches a generated Perception Cargo.toml for standalone crate
# publication: adds package exclusions.
################################################################

set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <path-to-Cargo.toml>" >&2
    exit 2
fi

cargo_toml="$1"

sed -i \
    -e '/^\[package\]$/a exclude = ["vendor/**", ".cargo/**", "crates/**", "Cargo.toml.orig"]' \
    "$cargo_toml"

grep -Fqx 'exclude = ["vendor/**", ".cargo/**", "crates/**", "Cargo.toml.orig"]' "$cargo_toml"
grep -Eq '^flatbuffers = "=[^"]+"$' "$cargo_toml"
