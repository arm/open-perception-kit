#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

if [[ "${GITHUB_ACTIONS:-}" == "true" ]]; then
    : "${GITHUB_RUN_ID:?GITHUB_RUN_ID is required in GitHub Actions}"
    : "${GITHUB_RUN_ATTEMPT:?GITHUB_RUN_ATTEMPT is required in GitHub Actions}"
    printf 'github-%s-%s\n' "${GITHUB_RUN_ID}" "${GITHUB_RUN_ATTEMPT}"
else
    printf 'local-%s-%s\n' \
        "$(date -u +%Y%m%dT%H%M%SZ)" \
        "$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')"
fi
