#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

destination="${1:?Usage: download_blackduck_detect.sh DESTINATION}"
detect_sha256="37c678bb1cb770fdf0e841c2d281aaf06abec2fa211bee33de6ddd04e13b84fd" # pragma: allowlist secret
detect_url="https://artifactory.arm.com/artifactory/synopsys-blackduck.detect-arm/detect-arm.sh"

install -d -m 0777 "$destination/output"
curl --proto '=https' --tlsv1.2 --fail --location --retry 3 --retry-all-errors \
    --output "$destination/detect-arm.sh" "$detect_url"
echo "$detect_sha256  $destination/detect-arm.sh" | sha256sum --check --strict
chmod +x "$destination/detect-arm.sh"
