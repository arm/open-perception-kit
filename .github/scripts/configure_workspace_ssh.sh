#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

key_specs=(
    "DEPLOY_KEY_FLOWDATA_SDK|flowdata_sdk|gh-flowdata-sdk|Arm-Debug/flowdata-sdk"
)

for spec in "${key_specs[@]}"; do
    IFS='|' read -r env_name _ _ _ <<< "$spec"
    if [[ -z "${!env_name:-}" ]]; then
        echo "Error: missing environment variable ${env_name}." >&2
        exit 1
    fi
done

if [[ -n "${RUNNER_TEMP:-}" ]]; then
    export HOME="${RUNNER_TEMP}/workspace-ssh-home"
fi

mkdir -p "$HOME/.ssh/keys"
chmod 700 "$HOME/.ssh" "$HOME/.ssh/keys"

write_key() {
    local name="$1"
    local content="$2"
    local path="$HOME/.ssh/keys/$name"

    printf '%s\n' "$content" | tr -d '\r' > "$path"
    chmod 600 "$path"
}

write_host() {
    local alias="$1"
    local key_name="$2"

    cat >> "$HOME/.ssh/config" << EOF
Host $alias
  HostName ssh.github.com
  Port 443
  User git
  IdentityFile $HOME/.ssh/keys/$key_name
  IdentitiesOnly yes
  StrictHostKeyChecking accept-new
  UserKnownHostsFile $HOME/.ssh/known_hosts
  ConnectTimeout 10

EOF
}

: > "$HOME/.ssh/config"

for spec in "${key_specs[@]}"; do
    IFS='|' read -r env_name key_name host_alias repo <<< "$spec"
    write_key "$key_name" "${!env_name}"
    write_host "$host_alias" "$key_name"

    target="git@${host_alias}:${repo}"
    sources=(
        "http://github.com/${repo}"
        "https://github.com/${repo}"
        "git@github.com:${repo}"
        "git@ssh.github.com:${repo}"
        "ssh://git@github.com/${repo}"
        "ssh://git@ssh.github.com:443/${repo}"
    )
    for source in "${sources[@]}"; do
        git config --global --add "url.${target}.insteadOf" "$source"
        git config --global --add "url.${target}.git.insteadOf" "${source}.git"
    done
done

chmod 600 "$HOME/.ssh/config"

git config --global core.sshCommand "ssh -F $HOME/.ssh/config"
