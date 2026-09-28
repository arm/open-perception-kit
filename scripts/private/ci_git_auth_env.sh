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

configure_github_git_auth_env() {
    local github_token="$1"
    if [ -z "${github_token}" ]; then
        echo "GitHub token is required to configure authenticated git fetches." >&2
        return 1
    fi

    local git_basic_auth
    git_basic_auth="$(printf 'x-access-token:%s' "${github_token}" | base64 | tr -d '\n')"
    export GIT_CONFIG_COUNT=1
    export GIT_CONFIG_KEY_0=http.https://github.com/.extraheader
    export GIT_CONFIG_VALUE_0="AUTHORIZATION: basic ${git_basic_auth}"
    echo "::add-mask::${git_basic_auth}"
    echo "::add-mask::${GIT_CONFIG_VALUE_0}"
}
