#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
detect_script="${repo_root}/scripts/quick-start/detect-environment.sh"

if [[ "${OPK_PERCEPTION_IN_CONTAINER:-0}" != "1" && ! -f /.dockerenv && "${repo_root}" != "/work" ]]; then
    if ! command -v docker > /dev/null 2>&1; then
        echo "docker is required on the host; open-perception-kit commands run inside the OPK container" >&2
        exit 127
    fi

    container="${OPK_CONTAINER:-}"
    if [[ -z "${container}" ]]; then
        if ! detect_output="$("${detect_script}" --shell)"; then
            eval "${detect_output}"
            echo "unsupported host platform: ${OPK_PLATFORM_NAME:-unknown}" >&2
            if [[ -n "${OPK_UNSUPPORTED_REASON:-}" ]]; then
                echo "reason: ${OPK_UNSUPPORTED_REASON}" >&2
            fi
            exit 1
        fi
        eval "${detect_output}"
        container="${OPK_CONTAINER_NAME}"
    fi

    if ! docker inspect -f '{{.State.Running}}' "${container}" 2> /dev/null | grep -q '^true$'; then
        echo "container '${container}' is not running" >&2
        echo "start it with ./scripts/quick_start.sh or override it with OPK_CONTAINER" >&2
        exit 1
    fi

    exec docker exec --user dev --workdir /work --env OPK_PERCEPTION_IN_CONTAINER=1 \
        "${container}" ./scripts/perception-sdk.sh "$@"
fi

export GIT_CONFIG_COUNT=1
export GIT_CONFIG_KEY_0=safe.directory
export GIT_CONFIG_VALUE_0="${repo_root}"

cd "${repo_root}"
exec python3 tools/perception/cli.py "$@"
