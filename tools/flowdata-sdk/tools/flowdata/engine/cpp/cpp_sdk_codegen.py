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

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from ..errors import fail
from ..flatbuffers_compat import cpp_flatbuffers_requirement
from ..types import RESERVED_PAYLOAD_ID_MIN, SchemaEntry, SemanticVersion
from .header_template import ENVELOPE_SCHEMA_TEMPLATE, HEADER_TEMPLATE


def generate_header(
    entries: list[SchemaEntry],
    sdk_name: str,
    sdk_version: SemanticVersion,
    flatc_version: SemanticVersion,
    schema_set_digest: str,
) -> str:
    includes = "\n".join(f'#include "{entry.generated_header_path.as_posix()}"' for entry in entries)

    trait_specializations = "\n\n".join(
        f"""template <>
struct native_traits<{entry.native_type}> {{
    using table_type = {entry.table_type};
    using native_type = {entry.native_type};

    static constexpr id_t id = {entry.numeric_id}ULL;
    static constexpr std::string_view qualified_root_type = "{entry.qualified_root_type}";

    static constexpr const char* file_identifier() {{
        return \"{entry.file_identifier}\";
    }}

    static auto pack(flatbuffers::FlatBufferBuilder& builder, const native_type* obj) {{
        return {entry.create_fn}(builder, obj);
    }}
}};"""
        for entry in entries
    )

    payload_variant_types = "".join(f",\n        {entry.native_type}" for entry in entries)

    result = HEADER_TEMPLATE
    result = result.replace('{includes}', includes)
    result = result.replace('{trait_specializations}', trait_specializations)
    result = result.replace('{payload_variant_types}', payload_variant_types)
    result = result.replace('{external_key_min}', str(RESERVED_PAYLOAD_ID_MIN))
    result = result.replace('__SDK_VERSION__', str(sdk_version))
    result = result.replace('__SCHEMA_SET_SHA256__', schema_set_digest)
    result = result.replace(
        '__FLATBUFFERS_VERSION_REQUIREMENT__',
        cpp_flatbuffers_requirement(flatc_version),
    )
    result = result.replace('__SDK_NAME_UPPER__', sdk_name.upper())
    result = result.replace('__SDK_NAME__', sdk_name)
    # format-style template used doubled braces for literal '{' and '}'.
    result = result.replace('{{', '{').replace('}}', '}')
    return result


def _flatc_include_args(include_dirs: list[Path]) -> list[str]:
    args: list[str] = []
    for include_dir in include_dirs:
        args.extend(["-I", str(include_dir)])
    return args


def run_flatc(
    schema_paths: list[Path],
    fb_out_dir: Path,
    flatc_bin: str,
    *,
    include_dirs: list[Path] | None = None,
) -> None:
    include_dirs = include_dirs or []
    try:
        result = subprocess.run(
            [
                flatc_bin,
                "--cpp",
                "--gen-object-api",
                "--scoped-enums",
                "-o",
                str(fb_out_dir),
                *_flatc_include_args(include_dirs),
                *(str(schema_path) for schema_path in schema_paths),
            ],
            check=True,
            text=True,
            capture_output=True,
        )
    except FileNotFoundError:
        fail(f"flatc not found: {flatc_bin}")
    except subprocess.CalledProcessError as exc:
        stderr = exc.stderr.strip() if exc.stderr else ""
        stdout = exc.stdout.strip() if exc.stdout else ""
        details = "\n".join(part for part in [stdout, stderr] if part)
        schemas = ", ".join(str(schema_path) for schema_path in schema_paths)
        fail(
            f"flatc failed for schema set {schemas} (exit code {exc.returncode})"
            + (f"\n{details}" if details else "")
        )

    if result.stderr:
        print(result.stderr.strip(), file=sys.stderr)


def generate_flatbuffers_cpp(
    entries: list[SchemaEntry],
    sdk_name: str,
    schema_dir: Path,
    schema_paths: list[Path],
    generated_root: Path,
    flatc_bin: str,
) -> list[Path]:
    generated_files: list[Path] = []
    fb_out_dir = generated_root / "fb"
    if fb_out_dir.exists():
        shutil.rmtree(fb_out_dir)
    fb_out_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix=f"{sdk_name}-envelope-") as tmp_dir:
        tmp_path = Path(tmp_dir)
        envelope_schema = tmp_path / "wire_envelope.fbs"
        envelope_schema.write_text(
            ENVELOPE_SCHEMA_TEMPLATE.replace("__SDK_NAME__", sdk_name),
            encoding="utf-8",
        )
        run_flatc([envelope_schema], tmp_path, flatc_bin)
        envelope_generated_src = tmp_path / "wire_envelope_generated.h"
        envelope_generated_dst = fb_out_dir / "internal" / "wire_envelope_generated.h"
        envelope_generated_dst.parent.mkdir(parents=True, exist_ok=True)
        envelope_generated_dst.write_bytes(envelope_generated_src.read_bytes())
        generated_files.append(envelope_generated_dst)

    del entries
    run_flatc(schema_paths, fb_out_dir, flatc_bin, include_dirs=[schema_dir])
    generated_files.extend(sorted(fb_out_dir.glob("*_generated.h")))

    return sorted(set(generated_files))
