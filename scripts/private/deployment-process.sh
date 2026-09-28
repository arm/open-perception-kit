#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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
# Ensure we are in the scripts directory

OPK_PIPELINE=${OPK_PIPELINE:-"yolo26n-320"}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"/../../

sudo chown -R $(id -u):$(id -g) "/work/" || true
sudo chmod +x ./tools/opk-menu ./scripts/serve-docs-plain.sh ./scripts/build.sh ./scripts/gen-doc.sh ./.devcontainer/setup.sh ./.devcontainer/platform_init.sh 2> /dev/null || true

.devcontainer/setup.sh
.devcontainer/platform_init.sh opk-dev disabled
./scripts/build.sh clean
./scripts/build.sh debug false
./scripts/gen-doc.sh

# Run scripts and redirect output to container log
./scripts/serve-docs-plain.sh &
./tools/opk-menu "$OPK_PIPELINE"

while true; do wait; done
