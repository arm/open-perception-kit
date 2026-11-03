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

import ast
import io
import re
import subprocess
import shutil
import tempfile
import textwrap
import tokenize
from pathlib import Path

from ..errors import fail
from ..flatbuffers_compat import FLATBUFFERS_PYTHON_REQUIREMENT
from ..python_compat import (
    PYTHON_BUILD_BACKEND,
    PYTHON_VERSION_REQUIREMENT,
)
from ..types import RESERVED_PAYLOAD_ID_MIN, GenerationContext, SchemaEntry
from ..wire_schema import ENVELOPE_SCHEMA_TEMPLATE
from .api_templates import GUEST_STUB_TEMPLATE, GUEST_TEMPLATE, PACKET_TEMPLATE
from .runtime_template import SDK_TEMPLATE

PYTHON_GENERATED_BANNER = (
    "# Generated file. Do not edit.\n"
    "# SDK users: change schemas or generator inputs, then regenerate this file.\n\n"
)


def _python_project_root(ctx: GenerationContext) -> Path:
    return getattr(ctx, "python_root", ctx.generated_root / "python")


def _python_namespace(namespace: str) -> str:
    return namespace.replace("::", ".")


def _python_fb_module(sdk_name: str, namespace: str, root_type_name: str) -> str:
    return f"{sdk_name}.fb.{_python_namespace(namespace)}.{root_type_name}"


def _flatc_include_args(include_dirs: list[Path]) -> list[str]:
    args: list[str] = []
    for include_dir in include_dirs:
        args.extend(["-I", str(include_dir)])
    return args


def _run_flatc_python(
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
                "--python",
                "--gen-object-api",
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
            part.strip()
            for part in [exc.stdout or "", exc.stderr or ""]
            if part.strip()
        )
        schema_list = ", ".join(str(schema) for schema in schemas)
        fail(
            f"flatc failed for schema set {schema_list} (exit code {exc.returncode})"
            + (f"\n{details}" if details else "")
        )


def _ensure_init_files(root: Path) -> list[Path]:
    files: list[Path] = []

    for directory in sorted(path for path in root.rglob("*") if path.is_dir()):
        if "__pycache__" in directory.parts:
            continue

        init = directory / "__init__.py"
        if not init.exists():
            init.write_text("", encoding="utf-8")
        files.append(init)

    return files


def _escape_legacy_python_keywords(text: str) -> str:
    # Older flatc emits these as identifiers in synchronous field helpers.
    # Token rewriting preserves strings/comments and is safe before ast.parse.
    return tokenize.untokenize(
        token._replace(string=token.string + "_")
        if token.type == tokenize.NAME and token.string in {"async", "await"} else token
        for token in tokenize.generate_tokens(io.StringIO(text).readline)
    )


def _rewrite_schema_imports(fb_root: Path, sdk_name: str) -> None:
    modules = {
        ".".join(path.relative_to(fb_root).with_suffix("").parts):
            (path, path.read_text(encoding="utf-8"))
        for path in sorted(fb_root.rglob("*.py")) if path.name != "__init__.py"
    }
    normalized = {name: _escape_legacy_python_keywords(text) for name, (_, text) in modules.items()}
    trees = {name: ast.parse(text) for name, text in normalized.items()}
    symbols = {
        f"{name}.{node.name}" for name, tree in trees.items() for node in tree.body
        if isinstance(node, (ast.ClassDef, ast.FunctionDef))
    }
    if not symbols:
        return
    qualified_ref = re.compile(r"(?<![A-Za-z0-9_\.])(?:" + "|".join(map(re.escape, sorted(symbols))) + r")\b")

    def qualified_name(node: ast.expr) -> str:
        if isinstance(node, ast.Name):
            return node.id
        if isinstance(node, ast.Attribute):
            parent = qualified_name(node.value)
            return f"{parent}.{node.attr}" if parent else ""
        return ""

    for name, (module_path, original) in modules.items():
        text = normalized[name]
        prefixes: set[tuple[int, int]] = set()
        for node in ast.walk(trees[name]):
            if isinstance(node, ast.Import):
                prefixes.update((alias.lineno, alias.col_offset) for alias in node.names if alias.name in modules)
            elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module in modules:
                # flatc emits single-line "from <module> import <symbol>" statements.
                prefixes.add((node.lineno, node.col_offset + len("from ")))
            elif isinstance(node, ast.Attribute) and qualified_name(node) in symbols:
                prefixes.add((node.lineno, node.col_offset))
        lines = text.splitlines(keepends=True)
        tokens = []
        for token in tokenize.generate_tokens(io.StringIO(text).readline):
            line, column = token.start
            if token.type == tokenize.NAME and (line, len(lines[line - 1][:column].encode("utf-8"))) in prefixes:
                token = token._replace(string=f"{sdk_name}.fb.{token.string}")
            elif token.type == tokenize.COMMENT and token.string.startswith("# type:"):
                token = token._replace(string=qualified_ref.sub(lambda match: f"{sdk_name}.fb.{match.group()}", token.string))
            tokens.append(token)
        rewritten = tokenize.untokenize(tokens)
        if rewritten != original:
            module_path.write_text(rewritten, encoding="utf-8")


def _registry_text(entries: list[SchemaEntry], sdk_name: str) -> str:
    lines = [
        PYTHON_GENERATED_BANNER.rstrip(),
        "_TYPE_REGISTRY = {",
    ]

    for entry in entries:
        module = _python_fb_module(sdk_name, entry.namespace, entry.root_type_name)

        lines.extend(
            [
                f"    {entry.numeric_id}: {{",
                f"        'name': '{entry.name}',",
                f"        'module': '{module}',",
                f"        'root_type': '{entry.root_type_name}',",
                f"        'native_type': '{entry.root_type_name}T',",
                f"        'qualified_root_type': '{entry.qualified_root_type}',",
                f"        'file_identifier': '{entry.file_identifier}',",
                "    },",
            ]
        )

    lines.append("}")

    return "\n".join(lines) + "\n"


def _sdk_text(sdk_name: str, sdk_version: str, schema_set_digest: str) -> str:
    return PYTHON_GENERATED_BANNER + textwrap.dedent(
        SDK_TEMPLATE
    ).lstrip().replace("__SDK_NAME__", sdk_name).replace(
        "__SDK_VERSION__", sdk_version
    ).replace(
        "__SCHEMA_SET_SHA256__", schema_set_digest
    ).replace(
        "__EXTERNAL_KEY_MIN__",
        str(RESERVED_PAYLOAD_ID_MIN),
    )


def _packet_text() -> str:
    return PYTHON_GENERATED_BANNER + textwrap.dedent(PACKET_TEMPLATE).lstrip()


def _guest_text(sdk_name: str) -> str:
    return PYTHON_GENERATED_BANNER + textwrap.dedent(
        GUEST_TEMPLATE
    ).lstrip().replace("__SDK_NAME__", sdk_name).replace(
        "__BRIDGE_MODULE__",
        f"{sdk_name}_bridge",
    )


def _guest_stub_text() -> str:
    return PYTHON_GENERATED_BANNER + textwrap.dedent(GUEST_STUB_TEMPLATE).lstrip()


def generate_python_sdk(entries: list[SchemaEntry], ctx: GenerationContext) -> list[Path]:
    from ..schema_set import schema_set_sha256

    schema_set_digest = schema_set_sha256(ctx)
    project_root = _python_project_root(ctx)
    src_root = project_root / "src"
    package_root = src_root / ctx.sdk_name
    fb_root = package_root / "fb"
    if src_root.exists():
        shutil.rmtree(src_root)
    package_root.mkdir(parents=True, exist_ok=True)

    generated: list[Path] = []

    with tempfile.TemporaryDirectory(prefix=f"{ctx.sdk_name}-python-envelope-") as tmp:
        envelope_schema = Path(tmp) / "envelope.fbs"
        envelope_schema.write_text(
            ENVELOPE_SCHEMA_TEMPLATE.replace("__SDK_NAME__", ctx.sdk_name),
            encoding="utf-8",
        )
        _run_flatc_python([envelope_schema], src_root, ctx.flatc_bin)

    fb_root.mkdir(parents=True, exist_ok=True)
    _run_flatc_python(ctx.schema_paths, fb_root, ctx.flatc_bin, include_dirs=[ctx.schema_dir])
    _rewrite_schema_imports(fb_root, ctx.sdk_name)

    generated.extend(sorted(path for path in src_root.rglob("*.py") if "__pycache__" not in path.parts))
    generated.extend(_ensure_init_files(src_root))

    init_path = package_root / "__init__.py"
    version_constant = f"{ctx.sdk_name.upper()}_VERSION"
    init_path.write_text(
        PYTHON_GENERATED_BANNER
        + f'{version_constant} = "{ctx.sdk_version}"\n'
        + f'{ctx.sdk_name.upper()}_NAME = "{ctx.sdk_name}"\n'
        + f'SCHEMA_SET_SHA256 = "{schema_set_digest}"\n'
        + f'FLATBUFFERS_VERSION_REQUIREMENT = "{FLATBUFFERS_PYTHON_REQUIREMENT}"\n'
        + f"__version__ = {version_constant}\n\n"
        + "from .sdk import (\n"
        + "    EXTERNAL_KEY_MIN,\n"
        + "    ExternalKey,\n"
        + "    ProducerIdentityStatus,\n"
        + "    external_key,\n"
        + "    is_external_key,\n"
        + ")\n",
        encoding="utf-8",
    )
    generated.append(init_path)

    packet_path = package_root / "packet.py"
    packet_path.write_text(_packet_text(), encoding="utf-8")
    generated.append(packet_path)

    guest_path = package_root / "guest.py"
    guest_path.write_text(_guest_text(ctx.sdk_name), encoding="utf-8")
    generated.append(guest_path)

    guest_stub_path = package_root / "guest.pyi"
    guest_stub_path.write_text(_guest_stub_text(), encoding="utf-8")
    generated.append(guest_stub_path)

    typing_marker_path = package_root / "py.typed"
    typing_marker_path.write_text("", encoding="utf-8")
    generated.append(typing_marker_path)

    registry_path = package_root / "registry.py"
    registry_path.write_text(
        _registry_text(entries, ctx.sdk_name),
        encoding="utf-8",
    )
    generated.append(registry_path)

    sdk_path = package_root / "sdk.py"
    sdk_path.write_text(
        _sdk_text(ctx.sdk_name, str(ctx.sdk_version), schema_set_digest),
        encoding="utf-8",
    )
    generated.append(sdk_path)

    pyproject_path = project_root / "pyproject.toml"
    pyproject_path.write_text(
        "# Generated file. Do not edit.\n"
        "# SDK users: change schemas or generator inputs, then regenerate this file.\n\n"
        + textwrap.dedent(
            """
            [project]
            name = "__SDK_NAME__"
            version = "__SDK_VERSION__"
            description = "Generated __SDK_NAME__ Python SDK"
            requires-python = "__PYTHON_VERSION_REQUIREMENT__"
            dependencies = ["flatbuffers__FLATBUFFERS_VERSION_REQUIREMENT__"]

            [build-system]
            requires = ["setuptools"]
            build-backend = "__PYTHON_BUILD_BACKEND__"

            [tool.setuptools.packages.find]
            where = ["src"]

            [tool.setuptools.package-data]
            "*" = ["*.pyi", "py.typed"]
            """
        ).lstrip().replace("__SDK_NAME__", ctx.sdk_name).replace(
            "__SDK_VERSION__", str(ctx.sdk_version)
        ).replace(
            "__PYTHON_VERSION_REQUIREMENT__", PYTHON_VERSION_REQUIREMENT
        ).replace(
            "__PYTHON_BUILD_BACKEND__", PYTHON_BUILD_BACKEND
        ).replace(
            "__FLATBUFFERS_VERSION_REQUIREMENT__", FLATBUFFERS_PYTHON_REQUIREMENT
        ),
        encoding="utf-8",
    )
    generated.append(pyproject_path)

    return sorted(set(generated))
