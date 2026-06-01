#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

: "${USERNAME:=devgoblin}"
: "${HOST_UID:=}"
: "${HOST_GID:=}"

# If no remap requested, just run as current user
if [[ -z "${HOST_UID}" || -z "${HOST_GID}" ]]; then
	exec "$@"
fi

# Need root to remap ids
if [[ "$(id -u)" -ne 0 ]]; then
	echo "ERROR: need to start container as root to remap UID/GID (set --user root)" >&2
	exit 1
fi

# Ensure group exists with HOST_GID
if ! getent group "${HOST_GID}" >/dev/null; then
	groupadd -g "${HOST_GID}" "${USERNAME}" 2>/dev/null || groupadd -g "${HOST_GID}" hostgroup
fi

# Ensure user exists
if ! id -u "${USERNAME}" >/dev/null 2>&1; then
	useradd -m -s /bin/bash -u "${HOST_UID}" -g "${HOST_GID}" "${USERNAME}"
fi

# Update user/group ids
usermod -u "${HOST_UID}" "${USERNAME}" || true
groupmod -g "${HOST_GID}" "$(id -gn "${USERNAME}")" || true
usermod -g "${HOST_GID}" "${USERNAME}" || true

# Fix home ownership (keep it cheap)
chown -R "${HOST_UID}:${HOST_GID}" "/home/${USERNAME}" || true
mkdir -p /work
chown -R "${HOST_UID}:${HOST_GID}" /work || true
chown -R "${HOST_UID}:${HOST_GID}" /tmp/pekcomm || true

# Drop privileges
exec gosu "${HOST_UID}:${HOST_GID}" "$@"
