#!/usr/bin/env bash
################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################

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
