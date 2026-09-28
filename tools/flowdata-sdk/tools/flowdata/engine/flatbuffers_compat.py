# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass

from .types import SemanticVersion


FLATBUFFERS_MIN_VERSION = SemanticVersion(24, 3, 25)
FLATBUFFERS_MAX_VERSION_EXCLUSIVE = SemanticVersion(26, 0, 0)
FLATBUFFERS_SUPPORTED_VERSION_REQUIREMENT = ">=24.3.25,<26.0.0"
FLATBUFFERS_DYNAMIC_RUNTIME_REQUIREMENT = ">=24.3.25,<26.0.0"
FLATBUFFERS_PYTHON_REQUIREMENT = FLATBUFFERS_DYNAMIC_RUNTIME_REQUIREMENT
FLATBUFFERS_TYPESCRIPT_REQUIREMENT = ">=24.3.25 <26.0.0"
_VERSION_RE = re.compile(
    r"(?<!\d)(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?!\d)", re.ASCII
)


@dataclass(frozen=True)
class FlatBuffersCompiler:
    output: str
    version: SemanticVersion


def _version_tuple(version: SemanticVersion) -> tuple[int, int, int]:
    return version.major, version.minor, version.patch


def is_supported_flatbuffers_version(version: SemanticVersion) -> bool:
    return (
        _version_tuple(FLATBUFFERS_MIN_VERSION)
        <= _version_tuple(version)
        < _version_tuple(FLATBUFFERS_MAX_VERSION_EXCLUSIVE)
    )


def parse_flatbuffers_version(output: str) -> SemanticVersion:
    match = _VERSION_RE.search(output)
    if match is None:
        raise ValueError(f"cannot parse a stable MAJOR.MINOR.PATCH version from: {output!r}")
    return SemanticVersion(*(int(component) for component in match.groups()))


def require_supported_flatc(flatc_bin: str) -> FlatBuffersCompiler:
    try:
        result = subprocess.run(
            [flatc_bin, "--version"],
            check=True,
            text=True,
            capture_output=True,
        )
    except FileNotFoundError:
        raise ValueError(f"flatc not found: {flatc_bin}") from None
    except subprocess.CalledProcessError as exc:
        details = (exc.stdout or exc.stderr or "").strip()
        raise ValueError(
            f"flatc --version failed: {flatc_bin}" + (f"\n{details}" if details else "")
        ) from None

    output = result.stdout.strip() or result.stderr.strip()
    if not output:
        raise ValueError(f"flatc --version returned no version: {flatc_bin}")
    version = parse_flatbuffers_version(output)
    if not is_supported_flatbuffers_version(version):
        raise ValueError(
            f"unsupported flatc version {version}; required "
            f"{FLATBUFFERS_SUPPORTED_VERSION_REQUIREMENT}"
        )
    return FlatBuffersCompiler(output=output, version=version)


def cpp_flatbuffers_requirement(version: SemanticVersion) -> str:
    return f"=={version}"


def flatbuffers_runtime_contracts(
    sdk_kind: str,
    cpp_python_bridge: bool,
    compiler_version: SemanticVersion,
) -> list[dict[str, str]]:
    requirements: dict[str, str] = {}
    if sdk_kind == "cpp":
        requirements["cpp"] = cpp_flatbuffers_requirement(compiler_version)
        if cpp_python_bridge:
            requirements["python"] = FLATBUFFERS_DYNAMIC_RUNTIME_REQUIREMENT
    elif sdk_kind == "python":
        requirements["python"] = FLATBUFFERS_DYNAMIC_RUNTIME_REQUIREMENT
    elif sdk_kind == "ts":
        requirements["typescript"] = FLATBUFFERS_DYNAMIC_RUNTIME_REQUIREMENT
    elif sdk_kind == "rust":
        requirements["rust"] = cpp_flatbuffers_requirement(compiler_version)
    else:
        raise ValueError(f"unsupported SDK for FlatBuffers runtime contract: {sdk_kind}")
    return [
        {
            "language": language,
            "package": "flatbuffers",
            "version_requirement": requirements[language],
        }
        for language in sorted(requirements)
    ]
