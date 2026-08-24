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
RECORD_PATTERN = re.compile(r"\b(table|struct)\s+(\w+)\s*\{(.*?)\}", re.DOTALL)
ENUM_PATTERN = re.compile(
    r"\b(enum|union)\s+(\w+)(?:\s*:\s*(\w+))?\s*\{(.*?)\}", re.DOTALL
)
INCLUDE_PATTERN = re.compile(r'^\s*include\s+"([^"]+)"\s*;', re.MULTILINE)
ROOT_PATTERN = re.compile(r"\broot_type\s+(\w+)\s*;")
FILE_IDENTIFIER_PATTERN = re.compile(r'\bfile_identifier\s+"([^"]+)"\s*;')


@dataclass(frozen=True)
class Field:
    name: str
    type_name: str
    default: Optional[str]
    attributes: tuple[str, ...] = ()


@dataclass(frozen=True)
class Sequence:
    underlying_type: Optional[str]
    members: tuple[str, ...]


@dataclass(frozen=True)
class Schema:
    path: str
    source: str
    tables: dict[str, tuple[Field, ...]]
    structs: dict[str, tuple[Field, ...]]
    sequences: dict[str, Sequence]
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


def parse_field_attributes(value: str) -> tuple[str, tuple[str, ...]]:
    if not value.rstrip().endswith(")"):
        return value, ()
    attribute_start = value.rfind("(")
    if attribute_start < 0:
        return value, ()
    raw_attributes = value[attribute_start + 1:].rstrip()[:-1]
    attributes = tuple(
        sorted(
            " ".join(attribute.split())
            for attribute in raw_attributes.split(",")
            if attribute.strip()
        )
    )
    return value[:attribute_start], attributes


def parse_field(statement: str) -> Optional[Field]:
    declaration = statement.strip()
    if not declaration.endswith(";"):
        return None
    declaration = declaration[:-1].strip()
    if ";" in declaration:
        return None

    name, separator, value = declaration.partition(":")
    name = name.strip()
    valid_name = name and all(character == "_" or character.isalnum() for character in name)
    if not separator or not valid_name:
        return None

    value, attributes = parse_field_attributes(value)
    type_name, default_separator, default = value.partition("=")
    normalized_type = " ".join(type_name.split())
    normalized_default = " ".join(default.split()) if default_separator else None
    if not normalized_type or (default_separator and not normalized_default):
        return None
    return Field(name, normalized_type, normalized_default, attributes)


def parse_fields(body: str) -> tuple[Field, ...]:
    return tuple(
        field
        for statement in body.splitlines()
        if (field := parse_field(statement)) is not None
    )


def parse_sequence(body: str) -> tuple[str, ...]:
    members: list[str] = []
    for raw_member in body.split(","):
        member = " ".join(raw_member.split())
        if member:
            members.append(member)
    return tuple(members)


def parse_schema(path: str, source: str) -> Schema:
    clean_source = strip_comments(source)
    records = list(RECORD_PATTERN.finditer(clean_source))
    tables = {
        match.group(2): parse_fields(match.group(3))
        for match in records
        if match.group(1) == "table"
    }
    structs = {
        match.group(2): parse_fields(match.group(3))
        for match in records
        if match.group(1) == "struct"
    }
    sequences = {
        f"{match.group(1)}:{match.group(2)}": Sequence(
            underlying_type=match.group(3), members=parse_sequence(match.group(4))
        )
        for match in ENUM_PATTERN.finditer(clean_source)
    }
    root_match = ROOT_PATTERN.search(clean_source)
    identifier_match = FILE_IDENTIFIER_PATTERN.search(clean_source)
    return Schema(
        path=path,
        source=source,
        tables=tables,
        structs=structs,
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


def validate_file_identifier(path: str, identifier: str) -> list[Finding]:
    findings: list[Finding] = []
    try:
        identifier.encode("ascii")
    except UnicodeEncodeError:
        findings.append(Finding("error", path, "file_identifier must be ASCII"))
    if len(identifier) != 4:
        findings.append(Finding("error", path, "file_identifier must contain four characters"))
    return findings


def validate_root_table(path: str, schema: Schema) -> list[Finding]:
    root_fields = schema.tables.get(schema.root_type or "")
    if root_fields is None:
        return [Finding("error", path, "root_type must name a table in the same schema")]
    expected_versions = (
        Field("schema_major", "ushort", "1"),
        Field("schema_minor", "ushort", "0"),
    )
    if root_fields[:2] != expected_versions:
        return [
            Finding(
                "error",
                path,
                "root table must begin with schema_major:ushort = 1 and schema_minor:ushort = 0",
            )
        ]
    return []


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
        findings.extend(validate_file_identifier(path, identifier))
        if identifier in identifiers:
            findings.append(
                Finding("error", path, f"duplicate file_identifier also used by {identifiers[identifier]}")
            )
        identifiers[identifier] = path

        findings.extend(validate_root_table(path, schema))
    return findings


def compare_fields(
    path: str,
    record: str,
    old: tuple[Field, ...],
    new: tuple[Field, ...],
    *,
    fixed_layout: bool = False,
) -> list[Finding]:
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
                    f"{record} field {index} renamed or reordered: {old_field.name} -> {new_field.name}",
                )
            )
            continue
        if old_field.type_name != new_field.type_name:
            findings.append(
                Finding(
                    "breaking",
                    path,
                    f"{record}.{old_field.name} type changed: {old_field.type_name} -> {new_field.type_name}",
                )
            )
        if old_field.default != new_field.default:
            findings.append(
                Finding(
                    "breaking",
                    path,
                    f"{record}.{old_field.name} default changed: {old_field.default!r} -> {new_field.default!r}",
                )
            )
        if old_field.attributes != new_field.attributes:
            findings.append(
                Finding(
                    "breaking",
                    path,
                    f"{record}.{old_field.name} attributes changed: "
                    f"{old_field.attributes!r} -> {new_field.attributes!r}",
                )
            )
    if len(new) < len(old):
        removed = ", ".join(field.name for field in old[len(new):])
        findings.append(Finding("breaking", path, f"{record} removed trailing fields: {removed}"))
    elif len(new) > len(old):
        added_fields = new[len(old):]
        added = ", ".join(field.name for field in added_fields)
        has_required_field = any(
            any(attribute.split(":", 1)[0].strip() == "required" for attribute in field.attributes)
            for field in added_fields
        )
        severity = "breaking" if fixed_layout or has_required_field else "additive"
        findings.append(Finding(severity, path, f"{record} appended fields: {added}"))
    return findings


def compare_schema_identity(old: Schema, new: Schema) -> list[Finding]:
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
    return findings


def compare_records(old: Schema, new: Schema, kind: str) -> list[Finding]:
    findings: list[Finding] = []
    old_records = getattr(old, kind)
    new_records = getattr(new, kind)
    fixed_layout = kind == "structs"
    record_name = kind[:-1]
    for record in sorted(set(old_records) | set(new_records)):
        if record not in new_records:
            findings.append(Finding("breaking", new.path, f"removed {record_name} {record}"))
        elif record not in old_records:
            findings.append(Finding("additive", new.path, f"added {record_name} {record}"))
        else:
            findings.extend(
                compare_fields(
                    new.path,
                    record,
                    old_records[record],
                    new_records[record],
                    fixed_layout=fixed_layout,
                )
            )
    return findings


def compare_sequences(old: Schema, new: Schema) -> list[Finding]:
    findings: list[Finding] = []
    for sequence in sorted(set(old.sequences) | set(new.sequences)):
        old_sequence = old.sequences.get(sequence)
        new_sequence = new.sequences.get(sequence)
        if new_sequence is None:
            findings.append(Finding("breaking", new.path, f"removed {sequence}"))
        elif old_sequence is None:
            findings.append(Finding("additive", new.path, f"added {sequence}"))
        elif new_sequence.underlying_type != old_sequence.underlying_type:
            findings.append(
                Finding("breaking", new.path, f"changed underlying type of {sequence}")
            )
        elif new_sequence.members[: len(old_sequence.members)] != old_sequence.members:
            findings.append(Finding("breaking", new.path, f"reordered, removed, or changed {sequence}"))
        elif len(new_sequence.members) > len(old_sequence.members):
            findings.append(Finding("additive", new.path, f"appended values to {sequence}"))
    return findings


def compare_schema(old: Schema, new: Schema) -> list[Finding]:
    findings = compare_schema_identity(old, new)
    findings.extend(compare_records(old, new, "tables"))
    findings.extend(compare_records(old, new, "structs"))
    findings.extend(compare_sequences(old, new))

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

    bump = required_bump(findings, changed_paths)

    return {
        "base": base,
        "required_bump": bump,
        "changed_schemas": sorted(changed_paths),
        "affected_roots": dependent_roots(current, changed_paths),
        "findings": [finding.__dict__ for finding in findings],
    }


def print_report(report: dict[str, object]) -> None:
    print(f"Base: {report['base']}")
    print(f"Required PEK release impact: {report['required_bump']}")
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
        description="Evaluate Perception schema compatibility and PEK release impact against a Git base."
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
