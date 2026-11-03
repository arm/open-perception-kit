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

import subprocess
import shutil
import tempfile
import textwrap
from pathlib import Path

from ..errors import fail
from ..flatbuffers_compat import FLATBUFFERS_TYPESCRIPT_REQUIREMENT
from ..types import RESERVED_PAYLOAD_ID_MIN, GenerationContext, SchemaEntry
from ..wire_schema import ENVELOPE_SCHEMA_TEMPLATE
from .runtime_template import ENVELOPE_TEMPLATE

TS_GENERATED_BANNER = (
    "// Generated file. Do not edit.\n"
    "// SDK users: change schemas or generator inputs, then regenerate this file.\n\n"
)


def _ts_project_root(ctx: GenerationContext) -> Path:
    return ctx.generated_root / "ts"


def _flatc_include_args(include_dirs: list[Path]) -> list[str]:
    args: list[str] = []
    for include_dir in include_dirs:
        args.extend(["-I", str(include_dir)])
    return args


def _run_flatc_ts(
    schemas: list[Path],
    output_dir: Path,
    flatc: str,
    *,
    include_dirs: list[Path] | None = None,
) -> None:
    include_dirs = include_dirs or []
    try:
        subprocess.run(
            [
                flatc,
                "--ts",
                "--gen-object-api",
                "--gen-all",
                "-o",
                str(output_dir),
                *_flatc_include_args(include_dirs),
                *(str(schema) for schema in schemas),
            ],
            check=True,
            text=True,
            capture_output=True,
        )
    except FileNotFoundError:
        fail(f"flatc not found: {flatc}")
    except subprocess.CalledProcessError as exc:
        details = "\n".join(
            part.strip() for part in [exc.stdout or "", exc.stderr or ""] if part.strip()
        )
        schema_list = ", ".join(str(schema) for schema in schemas)
        fail(
            f"flatc failed for schema set {schema_list} (exit code {exc.returncode})"
            + (f"\n{details}" if details else "")
            + "\nEnsure this flatc build supports the --ts target."
        )


def _namespace_module_path(entry: SchemaEntry) -> str:
    return "/".join(_ts_file_stem(part) for part in entry.namespace.split("::"))


def _ts_file_stem(name: str) -> str:
    result: list[str] = []
    previous_was_separator = True

    for index, char in enumerate(name):
        if char.isalnum():
            if (
                char.isupper()
                and index > 0
                and not previous_was_separator
                and not name[index - 1].isupper()
            ):
                result.append("-")
            result.append(char.lower())
            previous_was_separator = False
            continue

        if not previous_was_separator:
            result.append("-")
            previous_was_separator = True

    return "".join(result).strip("-") or name.lower()


def _entrypoint_module_path(entry: SchemaEntry) -> str:
    namespace_path = _namespace_module_path(entry)
    return f"{namespace_path}/{_ts_file_stem(entry.root_type_name)}"


def _registry_text(entries: list[SchemaEntry]) -> str:
    import_lines: list[str] = [
      TS_GENERATED_BANNER.rstrip(),
      "import { ByteBuffer } from 'flatbuffers';",
    ]
    factory_lines: list[str] = []
    types_map_lines: list[str] = ["export const _TYPE_REGISTRY = new Map<bigint, TypeInfo>(["]
    class_map_lines: list[str] = ["export const _CLASS_TO_ID = new Map<PayloadClass, bigint>(["]

    for entry in entries:
        module_path = _entrypoint_module_path(entry)
        import_name = entry.root_type_name
        native_name = f"{entry.root_type_name}T"
        root_alias = f"{import_name}_{entry.numeric_id}"
        native_alias = f"{native_name}_{entry.numeric_id}"
        import_lines.append(
          f"import {{ {import_name} as {root_alias}, {native_name} as {native_alias} }} "
          f"from './fb/{module_path}.js';"
        )

        factory_name = f"decode_{entry.numeric_id}"
        factory_lines.append(
          f"const {factory_name} = (blob: Uint8Array): unknown => "
          f"{root_alias}.getRootAs{import_name}(new ByteBuffer(blob)).unpack();"
        )
        # verify function using bufferHasIdentifier
        verify_name = f"verify_{entry.numeric_id}"
        factory_lines.append(
          f"const {verify_name} = (blob: Uint8Array): boolean => "
          f"{root_alias}.bufferHasIdentifier(new ByteBuffer(blob));"
        )

        types_map_lines.extend(
            [
                f"  [{entry.numeric_id}n, {{",
                f"    name: '{entry.name}',",
                f"    root_type: '{entry.root_type_name}',",
                f"    qualified_root_type: '{entry.qualified_root_type}',",
                f"    file_identifier: '{entry.file_identifier}',",
            f"    decode: {factory_name},",
            f"    verify: {verify_name},",
                "  }],",
            ]
        )
        class_map_lines.append(f"  [{native_alias}, {entry.numeric_id}n],")

    types_map_lines.append("]);")
    class_map_lines.append("]);")

    return "\n".join(
        [
            *import_lines,
        "export type PayloadClass<T = unknown> = Function & { prototype: T };",
            "",
        "export type TypeInfo = {",
        "  name: string;",
        "  root_type: string;",
        "  qualified_root_type: string;",
        "  file_identifier: string;",
        "  decode: (blob: Uint8Array) => unknown;",
        "  verify?: (blob: Uint8Array) => boolean;",
        "};",
            "",
            *factory_lines,
            "",
            *types_map_lines,
            "",
            *class_map_lines,
        ]
    )


def _sdk_namespace_module_path(sdk_name: str) -> str:
    return "/".join(_ts_file_stem(part) for part in sdk_name.split("::"))


def _envelope_text(sdk_name: str, sdk_version: str, schema_set_digest: str) -> str:
    return TS_GENERATED_BANNER + textwrap.dedent(
        ENVELOPE_TEMPLATE
    ).lstrip().replace("__SDK_NAME__", sdk_name).replace(
        "__SDK_VERSION__", sdk_version
    ).replace(
        "__SCHEMA_SET_SHA256__", schema_set_digest
    ).replace(
        "__EXTERNAL_KEY_MIN__",
        str(RESERVED_PAYLOAD_ID_MIN),
    ).replace(
        "__SDK_INTERNAL_NS_PATH__",
        _sdk_namespace_module_path(sdk_name),
    )


def _index_text(sdk_name: str, sdk_version: str, schema_set_digest: str) -> str:
    return TS_GENERATED_BANNER + textwrap.dedent(
        f"""
  export const {sdk_name.upper()}_VERSION = '{sdk_version}';
  export const {sdk_name.upper()}_NAME = '{sdk_name}';
  export const SCHEMA_SET_SHA256 = '{schema_set_digest}';
  export const FLATBUFFERS_VERSION_REQUIREMENT = '{FLATBUFFERS_TYPESCRIPT_REQUIREMENT}';
  export {{
    Envelope,
    ExternalKey,
    ProducerIdentityStatus,
    external_key,
    is_external_key,
    EXTERNAL_KEY_MIN,
  }} from './envelope.js';
        """
    ).lstrip()


def _package_json_text(sdk_name: str, sdk_version: str) -> str:
    return textwrap.dedent(
        """
        {
          "name": "__SDK_NAME__",
          "version": "__SDK_VERSION__",
          "description": "Generated __SDK_NAME__ TypeScript SDK",
          "type": "module",
          "main": "./dist/__SDK_NAME__/index.js",
          "types": "./dist/__SDK_NAME__/index.d.ts",
          "exports": {
            ".": {
              "types": "./dist/__SDK_NAME__/index.d.ts",
              "import": "./dist/__SDK_NAME__/index.js"
            },
            "./fb/*.js": {
              "types": "./dist/__SDK_NAME__/fb/*.d.ts",
              "import": "./dist/__SDK_NAME__/fb/*.js"
            }
          },
          "scripts": {
            "build": "tsc -p tsconfig.json",
            "typecheck": "tsc -p tsconfig.json --noEmit"
          },
          "dependencies": {
            "flatbuffers": "__FLATBUFFERS_VERSION_REQUIREMENT__"
          },
          "devDependencies": {
            "typescript": "^5.5.0"
          }
        }
        """
    ).lstrip().replace("__SDK_NAME__", sdk_name).replace("__SDK_VERSION__", sdk_version).replace(
        "__FLATBUFFERS_VERSION_REQUIREMENT__", FLATBUFFERS_TYPESCRIPT_REQUIREMENT
    )


def _tsconfig_text() -> str:
    return textwrap.dedent(
        """
        {
          "compilerOptions": {
            "target": "ES2020",
            "module": "ES2020",
            "moduleResolution": "Bundler",
            "strict": true,
            "declaration": true,
            "outDir": "dist",
            "rootDir": "src",
            "skipLibCheck": true,
            "forceConsistentCasingInFileNames": true
          },
          "include": ["src/**/*.ts"]
        }
        """
    ).lstrip()


def _write_text(path: Path, content: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    return path


def generate_typescript_sdk(entries: list[SchemaEntry], ctx: GenerationContext) -> list[Path]:
    from ..schema_set import schema_set_sha256

    schema_set_digest = schema_set_sha256(ctx)
    project_root = _ts_project_root(ctx)
    src_root = project_root / "src"
    package_root = src_root / ctx.sdk_name
    fb_root = package_root / "fb"
    if src_root.exists():
        shutil.rmtree(src_root)
    package_root.mkdir(parents=True, exist_ok=True)

    generated: list[Path] = []

    with tempfile.TemporaryDirectory(prefix=f"{ctx.sdk_name}-ts-envelope-") as tmp:
        envelope_schema = Path(tmp) / "envelope.fbs"
        envelope_schema.write_text(
            ENVELOPE_SCHEMA_TEMPLATE.replace("__SDK_NAME__", ctx.sdk_name),
            encoding="utf-8",
        )
        _run_flatc_ts([envelope_schema], fb_root, ctx.flatc_bin)

    _run_flatc_ts(ctx.schema_paths, fb_root, ctx.flatc_bin, include_dirs=[ctx.schema_dir])

    generated.extend(sorted(path for path in fb_root.rglob("*.ts") if "__pycache__" not in path.parts))

    generated.append(_write_text(package_root / "registry.ts", _registry_text(entries)))
    generated.append(
        _write_text(
            package_root / "envelope.ts",
            _envelope_text(ctx.sdk_name, str(ctx.sdk_version), schema_set_digest),
        )
    )
    generated.append(
        _write_text(
            package_root / "index.ts",
            _index_text(ctx.sdk_name, str(ctx.sdk_version), schema_set_digest),
        )
    )
    generated.append(
        _write_text(
            project_root / "package.json",
            _package_json_text(ctx.sdk_name, str(ctx.sdk_version)),
        )
    )
    generated.append(_write_text(project_root / "tsconfig.json", _tsconfig_text()))

    return sorted(set(generated))
