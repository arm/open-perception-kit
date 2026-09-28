# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

import argparse
import os
import re
import tempfile
from dataclasses import replace
from pathlib import Path
from typing import cast

from .cpp import get_integration_generators
from .discovery import discover_schema_set
from .errors import fail
from .flatbuffers_compat import require_supported_flatc
from .generators import get_sdk_generators, supported_sdk_names
from .manifest import manifest_root, verify_generation_manifest, write_generation_manifest
from .rust.identifiers import RUST_KEYWORDS
from .schema_set import assign_payload_ids
from .types import GenerationContext, SemanticVersion
from .validation import validate_entries, validate_schema_namespaces
from .version import GENERATOR_VERSION

DEFAULT_GENERATED_ROOT = Path("generated")
DEFAULT_SDK = "cpp"
_SDK_NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")
_SEMANTIC_VERSION_RE = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$", re.ASCII
)
_RESERVED_SDK_NAMES = {
    # C++ keywords and alternative tokens that cannot be used as namespace names.
    "alignas",
    "alignof",
    "and",
    "and_eq",
    "asm",
    "auto",
    "bitand",
    "bitor",
    "bool",
    "break",
    "case",
    "catch",
    "char",
    "char8_t",
    "char16_t",
    "char32_t",
    "class",
    "compl",
    "concept",
    "const",
    "consteval",
    "constexpr",
    "constinit",
    "const_cast",
    "continue",
    "co_await",
    "co_return",
    "co_yield",
    "decltype",
    "default",
    "delete",
    "do",
    "double",
    "dynamic_cast",
    "else",
    "enum",
    "explicit",
    "export",
    "extern",
    "false",
    "float",
    "for",
    "friend",
    "goto",
    "if",
    "inline",
    "int",
    "long",
    "mutable",
    "namespace",
    "new",
    "noexcept",
    "not",
    "not_eq",
    "nullptr",
    "operator",
    "or",
    "or_eq",
    "private",
    "protected",
    "public",
    "register",
    "reinterpret_cast",
    "requires",
    "return",
    "short",
    "signed",
    "sizeof",
    "static",
    "static_assert",
    "static_cast",
    "struct",
    "switch",
    "template",
    "this",
    "thread_local",
    "throw",
    "true",
    "try",
    "typedef",
    "typeid",
    "typename",
    "union",
    "unsigned",
    "using",
    "virtual",
    "void",
    "volatile",
    "wchar_t",
    "while",
    "xor",
    "xor_eq",
    # Python keywords that cannot be used in normal import statements.
    "as",
    "assert",
    "async",
    "await",
    "def",
    "del",
    "elif",
    "except",
    "finally",
    "from",
    "global",
    "import",
    "in",
    "is",
    "lambda",
    "match",
    "nonlocal",
    "pass",
    "raise",
    "with",
    "yield",
} | (RUST_KEYWORDS - {"Self"})


def _sdk_name_arg(value: str) -> str:
    if not _SDK_NAME_RE.fullmatch(value):
        raise argparse.ArgumentTypeError(
            "--name must match [a-z][a-z0-9_]* so it can be used as a C++ namespace, "
            "Python package, Rust crate, TypeScript package path, CMake target prefix, "
            "and Meson variable prefix"
        )
    if value in _RESERVED_SDK_NAMES:
        raise argparse.ArgumentTypeError(
            f"--name '{value}' is reserved by one of the generated target languages; "
            "choose a non-keyword package/namespace name"
        )
    return value


def _public_name_arg(value: str) -> str:
    if not _SDK_NAME_RE.fullmatch(value):
        raise argparse.ArgumentTypeError(
            "--public-name must match [a-z][a-z0-9_]*"
        )
    if value in _RESERVED_SDK_NAMES:
        raise argparse.ArgumentTypeError(
            f"--public-name '{value}' is reserved; choose a non-keyword name"
        )
    return value


def _semantic_version_arg(value: str) -> SemanticVersion:
    match = _SEMANTIC_VERSION_RE.fullmatch(value)
    if match is None:
        raise argparse.ArgumentTypeError(
            "--version must be a stable semantic version in MAJOR.MINOR.PATCH form "
            "without leading zeroes"
        )
    return SemanticVersion(*(int(component) for component in match.groups()))


def _build_context(
    sdk_name: str,
    sdk_version: SemanticVersion,
    schema_dir: Path,
    generated_root: Path,
    flatc_bin: str,
    cpp_python_bridge: bool = False,
    public_name: str | None = None,
) -> GenerationContext:
    package_dir = Path(__file__).resolve().parent
    entrypoint_path = package_dir.parent / "gen.py"

    tool_sources = sorted(package_dir.rglob("*.py"))
    if entrypoint_path not in tool_sources:
        tool_sources.append(entrypoint_path)
    tool_sources = sorted(p.resolve() for p in tool_sources)
    try:
        flatc = require_supported_flatc(flatc_bin)
    except ValueError as exc:
        fail(str(exc))

    return GenerationContext(
        sdk_name=sdk_name,
        sdk_version=sdk_version,
        schema_dir=schema_dir.resolve(),
        schema_paths=[],
        generated_root=generated_root.resolve(),
        entrypoint_path=entrypoint_path.resolve(),
        flatc_bin=flatc_bin,
        flatc_version=flatc.version,
        flatc_version_output=flatc.output,
        tool_sources=tool_sources,
        cpp_python_bridge=cpp_python_bridge,
        public_name=public_name,
    )


def _add_common_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--name",
        required=True,
        type=_sdk_name_arg,
        help="Schema namespace and default public SDK name. Must match [a-z][a-z0-9_]*.",
    )
    parser.add_argument(
        "--schema-dir",
        required=True,
        type=Path,
        help="Directory containing the complete FlatBuffers schema set",
    )
    parser.add_argument(
        "--generated-root",
        type=Path,
        default=DEFAULT_GENERATED_ROOT,
        help="Output root for generated artifacts",
    )
    parser.add_argument(
        "--flatc",
        default="flatc",
        help="Path to the flatc executable",
    )


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="sdkgen",
        description="Generate SDK artifacts and optional C++ build integrations from a schema directory.",
    )
    parser.add_argument(
        "--version",
        action="version",
        version=f"%(prog)s {GENERATOR_VERSION}",
        help="Show the flowdata-sdk generator version and exit.",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    generate_parser = subparsers.add_parser(
        "generate",
        help="Generate one SDK and optional C++ build integrations",
    )
    _add_common_arguments(generate_parser)
    generate_parser.add_argument(
        "--version",
        required=True,
        type=_semantic_version_arg,
        help="Generated SDK release version in stable MAJOR.MINOR.PATCH form.",
    )
    generate_parser.add_argument(
        "--sdk",
        choices=supported_sdk_names(),
        default=DEFAULT_SDK,
        help="SDK to generate. Defaults to: cpp",
    )
    generate_parser.add_argument(
        "--cmake",
        action="store_true",
        help="Generate CMake integration (cpp only)",
    )
    generate_parser.add_argument(
        "--meson",
        action="store_true",
        help="Generate Meson integration (cpp only)",
    )
    generate_parser.add_argument(
        "--cpp-python-bridge",
        action="store_true",
        help="Generate the optional embedded-Python live bridge (cpp only)",
    )
    generate_parser.add_argument(
        "--public-name",
        type=_public_name_arg,
        help="Public SDK name for all languages. Defaults to --name.",
    )
    verify_parser = subparsers.add_parser(
        "verify-manifest",
        help="Validate generation-manifest metadata and generated file hashes",
    )
    verify_parser.add_argument(
        "manifest",
        type=Path,
        help="Path to flowdata-manifest.json; its parent is the default sdk root",
    )
    verify_parser.add_argument(
        "--schema-root",
        type=Path,
        help="Verify manifest schema inputs against this schema directory",
    )

    return parser


def _run_generate(
    context: GenerationContext,
    sdk_kind: str,
    integration_names: list[str],
) -> int:
    if integration_names and sdk_kind != "cpp":
        fail("CMake and Meson integrations are supported only for --sdk cpp")

    if context.cpp_python_bridge and sdk_kind != "cpp":
        fail("--cpp-python-bridge is supported only for --sdk cpp")

    schema_paths, entries = discover_schema_set(context.schema_dir)
    validate_schema_namespaces(schema_paths, context.sdk_name)
    entries = assign_payload_ids(entries, context.schema_dir, context.flatc_bin)
    validate_entries(entries, context.sdk_name)
    context = replace(
        context,
        schema_paths=schema_paths,
    )

    target_root = manifest_root(context, sdk_kind)
    target_root.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix=f".{context.sdk_name}-{sdk_kind}-", dir=target_root.parent
    ) as temporary_directory:
        temporary_root = Path(temporary_directory)
        staged_context = cast(
            GenerationContext,
            replace(context, generated_root=temporary_root / "generated"),
        )
        staged_root = manifest_root(staged_context, sdk_kind)

        sdk_generators = get_sdk_generators([sdk_kind])
        if len(sdk_generators) != 1:
            fail(f"failed to resolve SDK generator: {sdk_kind}")

        generated_paths = list(sdk_generators[0].generate(staged_context, entries))
        for generator in get_integration_generators(integration_names):
            generated_paths.extend(generator.write_outputs(staged_context, entries))

        generated_manifest = write_generation_manifest(
            staged_context,
            entries,
            sdk_kind,
            integration_names,
            generated_paths,
        )
        generated_paths.append(generated_manifest)
        try:
            verify_generation_manifest(generated_manifest, context.schema_dir)
        except ValueError as exc:
            fail(f"generated manifest verification failed: {exc}")

        backup_root = temporary_root / "previous"
        installed = False
        try:
            if target_root.exists():
                os.replace(target_root, backup_root)
            os.replace(staged_root, target_root)
            installed = True
        except Exception:
            if installed and target_root.exists():
                os.replace(target_root, staged_root)
            if backup_root.exists():
                os.replace(backup_root, target_root)
            raise

        for path in sorted(generated_paths):
            relative = path.relative_to(staged_root)
            print(f"Wrote {target_root / relative}")

    return 0


def _run_verify_manifest(
    manifest_path: Path,
    schema_root: Path | None,
) -> int:
    try:
        manifest = verify_generation_manifest(
            manifest_path,
            schema_root.resolve() if schema_root is not None else None,
        )
    except ValueError as exc:
        fail(f"manifest verification failed: {exc}")
    print(
        f"Verified {manifest_path.resolve()} "
        f"({len(manifest['files'])} files, {len(manifest['payloads'])} payloads)"
    )
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)

    if args.command == "verify-manifest":
        return _run_verify_manifest(args.manifest, args.schema_root)

    schema_dir = getattr(args, "schema_dir", None)
    generated_root = getattr(args, "generated_root", DEFAULT_GENERATED_ROOT)
    flatc_bin = getattr(args, "flatc", "flatc")
    cpp_python_bridge = bool(getattr(args, "cpp_python_bridge", False))

    sdk_name_arg = getattr(args, "name", None)
    if sdk_name_arg is None:
        sdk_name_arg = ""

    if args.command == "generate":
        context = _build_context(
            sdk_name_arg,
            args.version,
            schema_dir,
            generated_root,
            flatc_bin,
            cpp_python_bridge,
            args.public_name,
        )
        sdk_kind = args.sdk or DEFAULT_SDK
        integration_names: list[str] = []
        if args.cmake:
            integration_names.append("cmake")
        if args.meson:
            integration_names.append("meson")
        return _run_generate(
            context=context,
            sdk_kind=sdk_kind,
            integration_names=integration_names,
        )

    fail(f"unknown command: {args.command}")


if __name__ == "__main__":
    raise SystemExit(main())
