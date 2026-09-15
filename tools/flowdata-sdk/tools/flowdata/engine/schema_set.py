################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import hashlib
import json
import subprocess
import tempfile
from dataclasses import replace
from pathlib import Path

from .errors import fail
from .types import GenerationContext, RESERVED_PAYLOAD_ID_MIN, SchemaEntry

_KNOWN_ID_SALT = "flowdata-known-payload-id-v2"


def _schema_source_hash(schema_path: Path) -> str:
    return hashlib.sha256(schema_path.read_bytes()).hexdigest()


def schema_file_records(context: GenerationContext) -> list[dict[str, object]]:
    return [
        {
            "path": schema_path.relative_to(context.schema_dir).as_posix(),
            "sha256": _schema_source_hash(schema_path),
            "size": schema_path.stat().st_size,
        }
        for schema_path in sorted(context.schema_paths)
    ]


def schema_paths_sha256(schema_dir: Path, schema_paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for schema_path in sorted(schema_paths):
        relative_path = schema_path.relative_to(schema_dir).as_posix().encode("utf-8")
        content = schema_path.read_bytes()
        digest.update(len(relative_path).to_bytes(8, "big"))
        digest.update(relative_path)
        digest.update(len(content).to_bytes(8, "big"))
        digest.update(content)
    return digest.hexdigest()


def schema_set_sha256(context: GenerationContext) -> str:
    return schema_paths_sha256(context.schema_dir, context.schema_paths)


def _find_dependency_path(token: str, schema_dir: Path) -> Path | None:
    path = Path(token)
    candidates = []
    if path.is_absolute():
        candidates.append(path)
    else:
        candidates.extend([Path.cwd() / path, schema_dir / path])

    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()

    return None


def _make_dependency_path(token: str, schema_dir: Path) -> Path:
    resolved = _find_dependency_path(token, schema_dir)
    if resolved is not None:
        return resolved

    fail(
        f"flatc dependency output referenced missing schema path: {token}\n"
        f"Schema directory: {schema_dir}\n"
        "Fix: ensure every include can be resolved from --schema-dir."
    )


def _split_make_words(text: str) -> list[str]:
    words: list[str] = []
    current: list[str] = []
    escaped = False

    for char in text:
        if escaped:
            current.append(char)
            escaped = False
            continue

        if char == "\\":
            escaped = True
            continue

        if char.isspace():
            if current:
                words.append("".join(current))
                current.clear()
            continue

        current.append(char)

    if escaped:
        current.append("\\")

    if current:
        words.append("".join(current))

    return words


def _resolve_make_dependencies(tokens: list[str], schema_dir: Path) -> list[Path]:
    dependencies: list[Path] = []
    index = 0

    while index < len(tokens):
        match: tuple[int, Path] | None = None
        for end in range(len(tokens), index, -1):
            token = " ".join(tokens[index:end])
            resolved = _find_dependency_path(token, schema_dir)
            if resolved is not None:
                match = (end, resolved)
                break

        if match is None:
            _make_dependency_path(tokens[index], schema_dir)

        index, resolved_path = match
        dependencies.append(resolved_path)

    return dependencies


def _parse_make_rule_dependencies(output: str, schema_dir: Path) -> list[Path]:
    normalized = output.replace("\\\r\n", " ").replace("\\\n", " ").strip()
    if not normalized:
        fail("flatc did not report schema dependencies with -M")

    if ":" not in normalized:
        fail(
            "flatc dependency output did not contain a make-rule separator ':'\n"
            f"Output:\n{output.strip()}"
        )

    _, deps_text = normalized.split(":", 1)
    dependencies = _resolve_make_dependencies(_split_make_words(deps_text), schema_dir)
    if not dependencies:
        fail(
            "flatc dependency output did not list any schemas\n"
            f"Output:\n{output.strip()}"
        )

    return sorted(set(dependencies))


def _payload_dependencies(entry: SchemaEntry, schema_dir: Path, flatc_bin: str) -> list[Path]:
    with tempfile.TemporaryDirectory(prefix="schema-deps-") as tmp:
        try:
            result = subprocess.run(
                [
                    flatc_bin,
                    "-M",
                    "-c",
                    "-o",
                    tmp,
                    "-I",
                    str(schema_dir),
                    str(entry.schema_path),
                ],
                check=True,
                text=True,
                capture_output=True,
            )
        except FileNotFoundError:
            fail(f"flatc not found: {flatc_bin}")
        except subprocess.CalledProcessError as exc:
            details = "\n".join(
                part.strip()
                for part in [exc.stdout or "", exc.stderr or ""]
                if part.strip()
            )
            fail(
                f"flatc failed while resolving dependencies for {entry.schema_path} "
                f"(exit code {exc.returncode})"
                + (f"\n{details}" if details else "")
            )

    dependencies = _parse_make_rule_dependencies(result.stdout, schema_dir)
    if entry.schema_path.resolve() not in dependencies:
        dependencies.append(entry.schema_path.resolve())
        dependencies = sorted(set(dependencies))
    return dependencies


def _generated_payload_id(entry: SchemaEntry, dependencies: list[Path]) -> int:
    canonical = {
        "algorithm": _KNOWN_ID_SALT,
        "qualified_root_type": entry.qualified_root_type,
        "file_identifier": entry.file_identifier,
        "schemas": sorted(_schema_source_hash(schema_path) for schema_path in dependencies),
    }
    encoded = json.dumps(canonical, sort_keys=True, separators=(",", ":")).encode("utf-8")
    digest = hashlib.sha256(encoded).digest()
    return (int.from_bytes(digest[:8], "big") % (RESERVED_PAYLOAD_ID_MIN - 1)) + 1


def assign_payload_ids(
    entries: list[SchemaEntry],
    schema_dir: Path,
    flatc_bin: str,
) -> list[SchemaEntry]:
    return [
        replace(
            entry,
            numeric_id=_generated_payload_id(
                entry,
                _payload_dependencies(entry, schema_dir, flatc_bin),
            ),
        )
        for entry in entries
    ]
