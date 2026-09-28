#!/usr/bin/env bash
################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################

set -euo pipefail

/work/.devcontainer/devsetup.sh

if [[ -f "${HOME}/configs/.gitconfig" ]]; then
    ln -sfn "${HOME}/configs/.gitconfig" "${HOME}/.gitconfig"
fi

exec sleep infinity
