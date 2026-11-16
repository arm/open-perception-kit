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

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <amd64|arm64> <output-directory>" >&2
    exit 2
fi

case "$1" in
    amd64) sha256=ec015e7dab028e1803ae38f7fed21e42fd392a2eddce3c6a6d016ed6170b4ddd ;;
    arm64) sha256=84d2c40bae3a1b27e9e43d1ed54be257486305d2432454e4597441e970339d97 ;;
    *)
        echo "Unsupported architecture: $1" >&2
        exit 2
        ;;
esac

deb="libexecutorch-dev-1.3.1-3-$1.deb"
mkdir -p "$2"
curl --fail --location --retry 3 --retry-all-errors --output "$2/$deb" \
    "https://artifacts.tools.arm.com/open-perception-kit/executorch/$deb"
echo "$sha256  $2/$deb" | sha256sum --check -
