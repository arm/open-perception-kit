#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
"""Evaluate Perception FlatBuffers schema compatibility against a Git base."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


SCHEMA_DIRECTORY = Path("schemas/perception/metadata")
SDK_DESCRIPTOR = Path("tools/perception/sdk.json")
SEMVER_PATTERN = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
TABLE_PATTERN = re.compile(r"\btable\s+(\w+)\s*\{(.*?)\}", re.DOTALL)
ENUM_PATTERN = re.compile(r"\b(enum|union)\s+(\w+)(?:\s*:\s*\w+)?\s*\{(.*?)\}", re.DOTALL)
FIELD_PATTERN = re.compile(r"^\s*(\w+)\s*:\s*([^=;]+?)(?:\s*=\s*([^;]+?))?\s*;\s*$")
INCLUDE_PATTERN = re.compile(r'^\s*include\s+"([^"]+)"\s*;', re.MULTILINE)
ROOT_PATTERN = re.compile(r"\broot_type\s+(\w+)\s*;")
FILE_IDENTIFIER_PATTERN = re.compile(r'\bfile_identifier\s+"([^"]+)"\s*;')


@dataclass(frozen=True)
class Field:
    name: str
    type_name: str
    default: Optional[str]


@dataclass(frozen=True)
class Schema:
    path: str
    source: str
    tables: dict[str, tuple[Field, ...]]
    sequences: dict[str, tuple[str, ...]]
    includes: tuple[str, ...]
    root_type: Optional[str]
    file_identifier: Optional[str]


@dataclass(frozen=True)
class Finding:
    severity: str
    path: str
    message: str


def run_git(repo_root: Path, arguments: list[str]) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo_root), *arguments],
        check=False,
        text=True,
        capture_output=True,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise RuntimeError(f"git {' '.join(arguments)} failed: {detail}")
    return result.stdout


def strip_comments(source: str) -> str:
    without_blocks = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return re.sub(r"//.*$", "", without_blocks, flags=re.MULTILINE)


def parse_fields(body: str) -> tuple[Field, ...]:
    fields: list[Field] = []
    for statement in body.splitlines():
        match = FIELD_PATTERN.match(statement)
        if not match:
            continue
        fields.append(
            Field(
                name=match.group(1),
                type_name=" ".join(match.group(2).split()),
                default=" ".join(match.group(3).split()) if match.group(3) else None,
            )
        )
    return tuple(fields)


def parse_sequence(body: str) -> tuple[str, ...]:
    members: list[str] = []
    for raw_member in body.split(","):
        member = " ".join(raw_member.split())
        if member:
            members.append(member)
    return tuple(members)


def parse_schema(path: str, source: str) -> Schema:
    clean_source = strip_comments(source)
    tables = {
        match.group(1): parse_fields(match.group(2))
        for match in TABLE_PATTERN.finditer(clean_source)
    }
    sequences = {
        f"{match.group(1)}:{match.group(2)}": parse_sequence(match.group(3))
        for match in ENUM_PATTERN.finditer(clean_source)
    }
    root_match = ROOT_PATTERN.search(clean_source)
    identifier_match = FILE_IDENTIFIER_PATTERN.search(clean_source)
    return Schema(
        path=path,
        source=source,
        tables=tables,
        sequences=sequences,
        includes=tuple(INCLUDE_PATTERN.findall(clean_source)),
        root_type=root_match.group(1) if root_match else None,
        file_identifier=identifier_match.group(1) if identifier_match else None,
    )


def load_current_schemas(repo_root: Path) -> dict[str, Schema]:
    schema_root = repo_root / SCHEMA_DIRECTORY
    return {
        path.relative_to(repo_root).as_posix(): parse_schema(
            path.relative_to(repo_root).as_posix(), path.read_text(encoding="utf-8")
        )
        for path in sorted(schema_root.glob("*.fbs"))
    }


def load_base_schemas(repo_root: Path, base: str) -> dict[str, Schema]:
    paths = run_git(
        repo_root,
        ["ls-tree", "-r", "--name-only", base, "--", SCHEMA_DIRECTORY.as_posix()],
    ).splitlines()
    schemas: dict[str, Schema] = {}
    for path in paths:
        if not path.endswith(".fbs"):
            continue
        source = run_git(repo_root, ["show", f"{base}:{path}"])
        schemas[path] = parse_schema(path, source)
    return schemas


def load_current_version(repo_root: Path) -> tuple[int, int, int]:
    descriptor = json.loads((repo_root / SDK_DESCRIPTOR).read_text(encoding="utf-8"))
    return parse_version(descriptor["version"], str(SDK_DESCRIPTOR))


def load_base_version(repo_root: Path, base: str) -> Optional[tuple[int, int, int]]:
    result = subprocess.run(
        ["git", "-C", str(repo_root), "show", f"{base}:{SDK_DESCRIPTOR.as_posix()}"],
        check=False,
        text=True,
        capture_output=True,
    )
    if result.returncode != 0:
        return None
    descriptor = json.loads(result.stdout)
    return parse_version(descriptor["version"], f"{base}:{SDK_DESCRIPTOR}")


def parse_version(value: str, source: str) -> tuple[int, int, int]:
    match = SEMVER_PATTERN.fullmatch(value)
    if not match:
        raise ValueError(f"{source} has invalid stable semantic version: {value!r}")
    return tuple(int(component) for component in match.groups())


def format_version(version: Optional[tuple[int, int, int]]) -> Optional[str]:
    return ".".join(str(component) for component in version) if version else None


def validate_current_schemas(schemas: dict[str, Schema]) -> list[Finding]:
    findings: list[Finding] = []
    roots: dict[str, str] = {}
    identifiers: dict[str, str] = {}

    for path, schema in schemas.items():
        if schema.root_type is None and schema.file_identifier is not None:
            findings.append(Finding("error", path, "file_identifier requires root_type"))
        if schema.root_type is not None and schema.file_identifier is None:
            findings.append(Finding("error", path, "transportable root requires file_identifier"))
        if schema.root_type is None:
            continue

        if schema.root_type in roots:
            findings.append(
                Finding("error", path, f"duplicate root_type also used by {roots[schema.root_type]}")
            )
        roots[schema.root_type] = path

        identifier = schema.file_identifier or ""
        try:
            identifier.encode("ascii")
        except UnicodeEncodeError:
            findings.append(Finding("error", path, "file_identifier must be ASCII"))
        if len(identifier) != 4:
            findings.append(Finding("error", path, "file_identifier must contain four characters"))
        if identifier in identifiers:
            findings.append(
                Finding("error", path, f"duplicate file_identifier also used by {identifiers[identifier]}")
            )
        identifiers[identifier] = path

        root_fields = schema.tables.get(schema.root_type)
        if root_fields is None:
            findings.append(Finding("error", path, "root_type must name a table in the same schema"))
            continue
        expected_versions = (
            Field("schema_major", "ushort", "1"),
            Field("schema_minor", "ushort", "0"),
        )
        if root_fields[:2] != expected_versions:
            findings.append(
                Finding(
                    "error",
                    path,
                    "root table must begin with schema_major:ushort = 1 and schema_minor:ushort = 0",
                )
            )
    return findings


def compare_fields(path: str, table: str, old: tuple[Field, ...], new: tuple[Field, ...]) -> list[Finding]:
    findings: list[Finding] = []
    shared_count = min(len(old), len(new))
    for index in range(shared_count):
        old_field = old[index]
        new_field = new[index]
        if old_field.name != new_field.name:
            findings.append(
                Finding(
                    "breaking",
                    path,
                    f"{table} field {index} renamed or reordered: {old_field.name} -> {new_field.name}",
                )
            )
            continue
        if old_field.type_name != new_field.type_name:
            findings.append(
                Finding(
                    "breaking",
                    path,
                    f"{table}.{old_field.name} type changed: {old_field.type_name} -> {new_field.type_name}",
                )
            )
        if old_field.default != new_field.default:
            findings.append(
                Finding(
                    "breaking",
                    path,
                    f"{table}.{old_field.name} default changed: {old_field.default!r} -> {new_field.default!r}",
                )
            )
    if len(new) < len(old):
        removed = ", ".join(field.name for field in old[len(new):])
        findings.append(Finding("breaking", path, f"{table} removed trailing fields: {removed}"))
    elif len(new) > len(old):
        added = ", ".join(field.name for field in new[len(old):])
        findings.append(Finding("additive", path, f"{table} appended fields: {added}"))
    return findings


def compare_schema(old: Schema, new: Schema) -> list[Finding]:
    findings: list[Finding] = []
    if old.root_type != new.root_type:
        findings.append(
            Finding("breaking", new.path, f"root_type changed: {old.root_type!r} -> {new.root_type!r}")
        )
    if old.file_identifier != new.file_identifier:
        findings.append(
            Finding(
                "breaking",
                new.path,
                f"file_identifier changed: {old.file_identifier!r} -> {new.file_identifier!r}",
            )
        )

    for table in sorted(set(old.tables) | set(new.tables)):
        if table not in new.tables:
            findings.append(Finding("breaking", new.path, f"removed table {table}"))
        elif table not in old.tables:
            findings.append(Finding("additive", new.path, f"added table {table}"))
        else:
            findings.extend(compare_fields(new.path, table, old.tables[table], new.tables[table]))

    for sequence in sorted(set(old.sequences) | set(new.sequences)):
        if sequence not in new.sequences:
            findings.append(Finding("breaking", new.path, f"removed {sequence}"))
        elif sequence not in old.sequences:
            findings.append(Finding("additive", new.path, f"added {sequence}"))
        elif new.sequences[sequence][: len(old.sequences[sequence])] != old.sequences[sequence]:
            findings.append(Finding("breaking", new.path, f"reordered, removed, or changed {sequence}"))
        elif len(new.sequences[sequence]) > len(old.sequences[sequence]):
            findings.append(Finding("additive", new.path, f"appended values to {sequence}"))

    if old.source != new.source and not findings:
        findings.append(
            Finding("warning", new.path, "schema text changed without a detected structural change")
        )
    return findings


def dependent_roots(schemas: dict[str, Schema], changed_paths: set[str]) -> list[str]:
    by_name = {Path(path).name: schema for path, schema in schemas.items()}

    def depends_on_changed(schema: Schema, visited: set[str]) -> bool:
        if schema.path in changed_paths:
            return True
        if schema.path in visited:
            return False
        visited.add(schema.path)
        return any(
            include in by_name and depends_on_changed(by_name[include], visited)
            for include in schema.includes
        )

    return sorted(
        schema.root_type
        for schema in schemas.values()
        if schema.root_type and depends_on_changed(schema, set())
    )


def required_bump(findings: list[Finding], changed_paths: set[str]) -> str:
    if any(finding.severity == "breaking" for finding in findings):
        return "major"
    if changed_paths:
        return "minor"
    return "none"


def version_satisfies(
    base: Optional[tuple[int, int, int]],
    current: tuple[int, int, int],
    required: str,
) -> bool:
    if required == "none" or base is None:
        return True
    if required == "major":
        return current[0] > base[0]
    return current[0] > base[0] or (current[0] == base[0] and current[1] > base[1])


def evaluate(repo_root: Path, base: str) -> dict[str, object]:
    current = load_current_schemas(repo_root)
    previous = load_base_schemas(repo_root, base)
    findings = validate_current_schemas(current)
    changed_paths: set[str] = set()

    for path in sorted(set(previous) | set(current)):
        if path not in current:
            changed_paths.add(path)
            findings.append(Finding("breaking", path, "removed schema file"))
        elif path not in previous:
            changed_paths.add(path)
            severity = "additive" if current[path].root_type else "warning"
            findings.append(Finding(severity, path, "added schema file"))
        elif previous[path].source != current[path].source:
            changed_paths.add(path)
            findings.extend(compare_schema(previous[path], current[path]))

    base_version = load_base_version(repo_root, base)
    current_version = load_current_version(repo_root)
    bump = required_bump(findings, changed_paths)
    if not version_satisfies(base_version, current_version, bump):
        findings.append(
            Finding(
                "error",
                str(SDK_DESCRIPTOR),
                f"SDK version {format_version(current_version)} does not satisfy required {bump} bump "
                f"from {format_version(base_version)}",
            )
        )

    return {
        "base": base,
        "base_version": format_version(base_version),
        "current_version": format_version(current_version),
        "required_bump": bump,
        "changed_schemas": sorted(changed_paths),
        "affected_roots": dependent_roots(current, changed_paths),
        "findings": [finding.__dict__ for finding in findings],
    }


def print_report(report: dict[str, object]) -> None:
    print(f"Base: {report['base']}")
    print(f"SDK version: {report['base_version']} -> {report['current_version']}")
    print(f"Required bump: {report['required_bump']}")
    changed = report["changed_schemas"]
    affected = report["affected_roots"]
    print("Changed schemas: " + (", ".join(changed) if changed else "none"))
    print("Affected roots: " + (", ".join(affected) if affected else "none"))
    findings = report["findings"]
    if not findings:
        print("Findings: none")
        return
    print("Findings:")
    for finding in findings:
        print(f"- {finding['severity'].upper()}: {finding['path']}: {finding['message']}")


def repository_root(argument: Optional[str]) -> Path:
    if argument:
        return Path(argument).resolve()
    result = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        check=True,
        text=True,
        capture_output=True,
    )
    return Path(result.stdout.strip())


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Evaluate Perception schema compatibility and SDK versioning against a Git base."
    )
    parser.add_argument(
        "--base",
        default="HEAD",
        help="Git revision used as the compatibility baseline (default: HEAD).",
    )
    parser.add_argument("--repo-root", help="Repository root; defaults to the current Git worktree.")
    parser.add_argument("--json", action="store_true", help="Emit the report as JSON.")
    arguments = parser.parse_args()

    try:
        report = evaluate(repository_root(arguments.repo_root), arguments.base)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    if arguments.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print_report(report)

    blocking = {"error"}
    return 1 if any(item["severity"] in blocking for item in report["findings"]) else 0


if __name__ == "__main__":
    raise SystemExit(main())
