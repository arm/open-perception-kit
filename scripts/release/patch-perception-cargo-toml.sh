#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
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
