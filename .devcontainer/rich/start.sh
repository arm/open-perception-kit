#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

/work/.devcontainer/devsetup.sh

if [[ -f "${HOME}/configs/.gitconfig" ]]; then
    ln -sfn "${HOME}/configs/.gitconfig" "${HOME}/.gitconfig"
fi

exec sleep infinity
