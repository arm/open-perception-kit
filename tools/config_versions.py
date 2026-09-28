#!/usr/bin/env python3
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

"""Read and compare descriptor versions declared by JSON Schemas."""

import json
import logging
import re
from pathlib import Path


SUPPORTED_VERSION_KEY = "x-opk-supported-version"
SEMVER = re.compile(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", re.ASCII)


def supported_version(schema_path: Path) -> str:
    """Return the version declared by a descriptor schema."""
    try:
        schema = json.loads(schema_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"Cannot read configuration schema {schema_path}: {error}") from error
    version = schema.get(SUPPORTED_VERSION_KEY) if isinstance(schema, dict) else None
    if not isinstance(version, str) or SEMVER.fullmatch(version) is None:
        raise ValueError(
            f"{schema_path}:/{SUPPORTED_VERSION_KEY} must be a MAJOR.MINOR.PATCH string"
        )
    return version


def check_version(version: object, schema_path: Path, source: object, logger: logging.Logger) -> None:
    """Reject malformed/major mismatches, warn on minor mismatches, ignore patch."""
    supported = supported_version(schema_path)
    if not isinstance(version, str) or SEMVER.fullmatch(version) is None:
        raise ValueError(f"{source}:/version must be a MAJOR.MINOR.PATCH string")
    major, minor, _ = supported.split(".")
    version_major, version_minor, _ = version.split(".")
    if version_major != major:
        raise ValueError(
            f"{source}:/version {version!r} has an incompatible major; "
            f"supported version: {supported}. Use matching tools or migrate the configuration."
        )
    if version_minor != minor:
        logger.warning(
            "%s:/version %s has a different minor; supported version: %s. Continuing.",
            source, version, supported,
        )
