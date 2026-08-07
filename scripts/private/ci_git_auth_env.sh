#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

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
