#!/usr/bin/env bash
################################################################
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
################################################################

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exec "${script_dir}/private/run-perception-sdk.sh" "$@"
