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

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLATFORM_ID="${1:-}"

if [[ -z "${PLATFORM_ID}" ]]; then
    detect_output="$("${SCRIPT_DIR}/../quick-start/detect-environment.sh" --shell)"
    eval "${detect_output}"
    PLATFORM_ID="${OPK_PLATFORM_ID}"
fi

case "${PLATFORM_ID}" in
    wsl | macos)
        echo enabled
        ;;
    *)
        echo disabled
        ;;
esac
