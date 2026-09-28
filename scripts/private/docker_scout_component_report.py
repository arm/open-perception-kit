#!/usr/bin/env python3
################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################

from __future__ import annotations

import argparse
import html
import json
import sys
from collections import defaultdict
from collections.abc import Iterable, Sequence
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from urllib.parse import quote, unquote

SEVERITY_SEQUENCE = ("CRITICAL", "HIGH", "MEDIUM", "LOW", "UNKNOWN")
SEVERITY_ORDER = {
    "CRITICAL": 0,
    "HIGH": 1,
    "MEDIUM": 2,
    "LOW": 3,
    "UNKNOWN": 4,
}
SEVERITY_COUNT_KEYS = {
    "CRITICAL": "critical",
    "HIGH": "high",
    "MEDIUM": "medium",
    "LOW": "low",
    "UNKNOWN": "unspecified",
}
SEVERITY_BADGE_STYLES = {
    "CRITICAL": ("critical", "8b1924"),
    "HIGH": ("high", "e25d68"),
    "MEDIUM": ("medium", "fbb552"),
    "LOW": ("low", "fce1a9"),
    "UNKNOWN": ("unspecified", "lightgrey"),
}


@dataclass(frozen=True)
class ComponentHint:
    name: str
    kind: str
    evidence_path: str
    priority: int


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Render Docker Scout SARIF findings into a compact component report.",
    )
    parser.add_argument("--service", required=True, help="Service name used in the CI matrix.")
    parser.add_argument("--sarif-file", required=True, type=Path)
    parser.add_argument("--markdown-output", required=True, type=Path)
    parser.add_argument("--json-output", required=True, type=Path)
    parser.add_argument(
        "--summary-limit",
        type=int,
        default=6,
        help="Maximum number of component blocks to emit to stdout summary.",
    )
    return parser.parse_args(argv)


def code_label(value: str) -> str:
    return f"<code>{html.escape(value)}</code>"


def shields_escape(value: str) -> str:
    return quote(value.replace("-", "--").replace("_", "__"), safe="")


def badge(label: str, message: str, color: str, label_color: str | None = None) -> str:
    src = (
        "https://img.shields.io/badge/"
        f"{shields_escape(label)}-{shields_escape(message)}-{color}"
    )
    if label_color:
        src += f"?labelColor={quote(label_color, safe='')}"
    alt = html.escape(f"{label}: {message}", quote=True)
    return f'<img alt="{alt}" src="{src}"/>'


def severity_badge(severity: str, value: str, short: bool = False) -> str:
    label, active_color = SEVERITY_BADGE_STYLES.get(
        severity,
        SEVERITY_BADGE_STYLES["UNKNOWN"],
    )
    label_text = label[:1].upper() if short else label
    color = active_color if value != "0" else "lightgrey"
    return badge(label_text, value, color)


def severity_count_key(severity: str) -> str:
    return SEVERITY_COUNT_KEYS.get(severity, "unspecified")


def finding_badge(finding: dict) -> str:
    label, label_color = SEVERITY_BADGE_STYLES.get(
        finding["severity"],
        SEVERITY_BADGE_STYLES["UNKNOWN"],
    )
    badge_html = badge(label, finding["cve"], "lightgrey", label_color=label_color)
    if not finding["help_uri"]:
        return badge_html
    return f'<a href="{html.escape(finding["help_uri"], quote=True)}">{badge_html}</a>'


def severity_count_labels(
    counts: dict[str, int],
    *,
    short: bool = False,
) -> str:
    badges = []
    for severity in SEVERITY_SEQUENCE:
        key = severity_count_key(severity)
        count = counts.get(key, 0)
        badges.append(severity_badge(severity, str(count), short=short))
    return " ".join(badges)


def score_value(raw_score: str) -> float:
    try:
        return float(raw_score)
    except (TypeError, ValueError):
        return -1.0


def optional_string(value: object) -> str:
    if value is None:
        return ""
    return str(value)


def dict_value(value: object) -> dict:
    if isinstance(value, dict):
        return value
    return {}


def list_value(value: object) -> list:
    if isinstance(value, list):
        return value
    return []


def string_list(value: object) -> list[str]:
    return [item for item in list_value(value) if isinstance(item, str)]


def first_string(values: object) -> str:
    for item in list_value(values):
        if isinstance(item, str):
            return item
    return ""


def strip_prefix(value: str, prefix: str) -> str:
    if value.startswith(prefix):
        return value[len(prefix):]
    return value


def package_type(purl: str) -> str:
    if not purl:
        return ""
    return strip_prefix(purl, "pkg:").split("/", 1)[0]


def package_name(purl: str) -> str:
    if not purl:
        return ""
    body = strip_prefix(purl, "pkg:").split("?", 1)[0]
    name_version = body.rsplit("/", 1)[-1]
    return unquote(name_version.split("@", 1)[0])


def package_version(purl: str) -> str:
    if not purl or "@" not in purl:
        return ""
    body = strip_prefix(purl, "pkg:").split("?", 1)[0]
    return unquote(body.rsplit("@", 1)[-1])


def normalize_severity(value: str | None, fallback_tags: Iterable[str] | None = None) -> str:
    if value:
        return value.upper()
    for tag in fallback_tags or ():
        upper = tag.upper()
        if upper in SEVERITY_ORDER:
            return upper
    return "UNKNOWN"


def unique_strings(values: Iterable[str]) -> list[str]:
    seen = set()
    ordered = []
    for value in values:
        if not value or value in seen:
            continue
        seen.add(value)
        ordered.append(value)
    return ordered


def binary_name(path: str) -> str:
    candidate = PurePosixPath(path)
    if len(candidate.parts) < 2 or candidate.parts[-2] not in {"bin", "sbin"}:
        return ""
    return candidate.name


def dist_info_distribution_name(path: str) -> str:
    for part in PurePosixPath(path).parts:
        if not part.endswith(".dist-info"):
            continue

        stem = part[: -len(".dist-info")]
        name, separator, version = stem.rpartition("-")
        if not separator or not version or not version[0].isdigit():
            continue
        return name
    return ""


def python_package_directory_name(path: str) -> str:
    parts = PurePosixPath(path).parts
    for index, part in enumerate(parts[:-1]):
        if part not in {"site-packages", "dist-packages"}:
            continue

        component = parts[index + 1]
        if component.endswith((".dist-info", ".egg-info")):
            return ""
        return component
    return ""


def python_component_name(path: str) -> str:
    component = dist_info_distribution_name(path)
    if component:
        return component
    return python_package_directory_name(path)


def derive_component_hints(purl: str, locations: list[str]) -> list[ComponentHint]:
    hints: list[ComponentHint] = []
    resolved_package_name = package_name(purl) or purl or "unknown"
    resolved_package_type = package_type(purl)

    for path in locations:
        executable = binary_name(path)
        if executable:
            hints.append(
                ComponentHint(
                    name=executable,
                    kind="binary",
                    evidence_path=path,
                    priority=0,
                )
            )
            continue

        if resolved_package_type == "pypi":
            python_name = python_component_name(path)
            if python_name:
                hints.append(
                    ComponentHint(
                        name=python_name,
                        kind="python-package",
                        evidence_path=path,
                        priority=1,
                    )
                )

    if not hints:
        hints.append(
            ComponentHint(
                name=resolved_package_name,
                kind="package",
                evidence_path=locations[0] if locations else "",
                priority=2,
            )
        )

    unique_hints: list[ComponentHint] = []
    seen = set()
    for hint in sorted(hints, key=lambda item: (item.priority, item.name, item.evidence_path)):
        key = (hint.name, hint.kind)
        if key in seen:
            continue
        seen.add(key)
        unique_hints.append(hint)
    return unique_hints


def extract_locations(result: dict) -> list[str]:
    return unique_strings(
        (
            dict_value(dict_value(location.get("physicalLocation")).get("artifactLocation")).get("uri", "")
            for location in list_value(result.get("locations"))
            if isinstance(location, dict)
        )
    )


def empty_severity_counts() -> dict[str, int]:
    return {
        "critical": 0,
        "high": 0,
        "medium": 0,
        "low": 0,
        "unspecified": 0,
    }


def compute_severity_counts(findings: Iterable[dict]) -> dict[str, int]:
    counts = empty_severity_counts()
    for finding in findings:
        counts[severity_count_key(finding["severity"])] += 1
    return counts


def component_sort_key(component: dict) -> tuple:
    return (
        -component["severity_counts"]["critical"],
        -component["severity_counts"]["high"],
        -component["severity_counts"]["medium"],
        -component["severity_counts"]["low"],
        -component["severity_counts"]["unspecified"],
        component["component"],
    )


def package_group_sort_key(package_group: dict) -> tuple:
    return (
        -package_group["severity_counts"]["critical"],
        -package_group["severity_counts"]["high"],
        -package_group["severity_counts"]["medium"],
        -package_group["severity_counts"]["low"],
        -package_group["severity_counts"]["unspecified"],
        package_group["package_name"],
        package_group["package_version"],
        package_group["package_type"],
    )


def format_finding_labels(finding: dict) -> str:
    labels = [finding_badge(finding)]
    if finding["score"]:
        labels.append(code_label(f"score {finding['score']}"))
    if finding["fixed_version"]:
        labels.append(code_label(f"fix {finding['fixed_version']}"))
    return " ".join(labels)


def format_component_summary(component: dict) -> str:
    labels = [
        code_label(component["kind"]),
        badge("findings", str(component["finding_count"]), "lightgrey"),
        severity_count_labels(component["severity_counts"], short=True),
    ]
    return f"<summary><strong>{html.escape(component['component'])}</strong> {' '.join(labels)}</summary>"


def build_package_groups(component: dict) -> list[dict]:
    grouped: dict[str, list[dict]] = defaultdict(list)
    for finding in component["findings"]:
        key = finding["package"] or finding["package_name"] or "unknown"
        grouped[key].append(finding)

    package_groups = []
    for key, package_findings in grouped.items():
        purl = package_findings[0]["package"]
        resolved_name = package_findings[0]["package_name"] or key or "unknown"
        package_groups.append(
            {
                "package_ref": purl,
                "package_name": resolved_name,
                "package_version": package_version(purl),
                "package_type": package_type(purl),
                "finding_count": len(package_findings),
                "severity_counts": compute_severity_counts(package_findings),
                "findings": package_findings,
            }
        )

    package_groups.sort(key=package_group_sort_key)
    return package_groups


def render_package_block(package_group: dict) -> list[str]:
    package_line = f"- Package: <strong>{html.escape(package_group['package_name'])}</strong>"
    if package_group["package_version"]:
        package_line += f" {code_label(package_group['package_version'])}"
    if package_group["package_type"]:
        package_line += f" ({html.escape(package_group['package_type'])})"

    lines = [package_line]

    if package_group["package_ref"]:
        lines.append(f"- Package ref: {code_label(package_group['package_ref'])}")

    lines.append("- Findings:")
    for finding in package_group["findings"]:
        lines.append(f"  - {format_finding_labels(finding)}")
    return lines


def render_component_block(component: dict) -> list[str]:
    lines = [
        "<details>",
        format_component_summary(component),
        "",
    ]

    if component["evidence_paths"]:
        evidence_labels = " ".join(code_label(path) for path in component["evidence_paths"][:3])
        lines.append(f"- Evidence: {evidence_labels}")

    for package_group in build_package_groups(component):
        lines.append("")
        lines.extend(render_package_block(package_group))

    lines.extend(["", "</details>"])
    return lines


def build_components(findings: list[dict]) -> list[dict]:
    grouped: dict[str, list[dict]] = defaultdict(list)
    for finding in findings:
        grouped[finding["primary_component"]].append(finding)

    components = []
    for component_name, component_findings in grouped.items():
        components.append(
            {
                "component": component_name,
                "kind": component_findings[0]["primary_component_kind"],
                "package_names": sorted(
                    {item["package_name"] for item in component_findings if item["package_name"]}
                ),
                "packages": sorted(
                    {item["package"] for item in component_findings if item["package"]}
                ),
                "evidence_paths": unique_strings(
                    [
                        item["primary_evidence_path"]
                        for item in component_findings
                        if item["primary_evidence_path"]
                    ]
                    + [path for item in component_findings for path in item["evidence_paths"]]
                ),
                "finding_count": len(component_findings),
                "severity_counts": compute_severity_counts(component_findings),
                "findings": component_findings,
            }
        )

    components.sort(key=component_sort_key)
    return components


def write_missing_reports(
    service: str,
    sarif_file: Path,
    markdown_output: Path,
    json_output: Path,
) -> None:
    write_unavailable_reports(
        service=service,
        sarif_file=sarif_file,
        markdown_output=markdown_output,
        json_output=json_output,
        summary_message="No `cves.sarif.json` file was produced for this job, so component resolution is unavailable.",
    )


def write_unavailable_reports(
    service: str,
    sarif_file: Path,
    markdown_output: Path,
    json_output: Path,
    summary_message: str,
    warning_detail: str = "",
) -> None:
    payload = {
        "service": service,
        "sarif_file": str(sarif_file),
        "sarif_present": sarif_file.exists(),
        "finding_count": 0,
        "component_count": 0,
        "severity_counts": empty_severity_counts(),
        "components": [],
        "warning_detail": warning_detail,
    }
    json_output.parent.mkdir(parents=True, exist_ok=True)
    markdown_output.parent.mkdir(parents=True, exist_ok=True)
    json_output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    markdown_output.write_text(
        "\n".join(
            [
                f"# Docker Scout Component Report for `{service}`",
                "",
                summary_message,
                "",
            ]
        ),
        encoding="utf-8",
    )
    print(f"### Docker Scout component report ({service})")
    print("")
    print(summary_message)
    if warning_detail:
        print("")
        print(f"Warning: {warning_detail}")


def main(argv: Sequence[str] | None = None) -> None:
    args = parse_args(argv)
    sarif_file = args.sarif_file.resolve()
    markdown_output = args.markdown_output.resolve()
    json_output = args.json_output.resolve()

    if not sarif_file.exists():
        write_missing_reports(
            service=args.service,
            sarif_file=sarif_file,
            markdown_output=markdown_output,
            json_output=json_output,
        )
        return

    try:
        data = json.loads(sarif_file.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, ValueError) as exc:
        write_unavailable_reports(
            service=args.service,
            sarif_file=sarif_file,
            markdown_output=markdown_output,
            json_output=json_output,
            summary_message=(
                "The `cves.sarif.json` file could not be parsed, so component resolution is unavailable."
            ),
            warning_detail=f"Failed to parse SARIF: {type(exc).__name__}: {exc}",
        )
        return

    if not isinstance(data, dict):
        data = {}
    findings: list[dict] = []

    for run in list_value(data.get("runs")):
        if not isinstance(run, dict):
            continue

        rules = {
            optional_string(rule.get("id")): rule
            for rule in list_value(dict_value(dict_value(run.get("tool")).get("driver")).get("rules"))
            if isinstance(rule, dict) and optional_string(rule.get("id"))
        }

        for result in list_value(run.get("results")):
            if not isinstance(result, dict):
                continue

            rule_id = optional_string(result.get("ruleId"))
            rule = rules.get(rule_id, {})
            properties = dict_value(rule.get("properties"))
            purl = first_string(properties.get("purls"))
            locations = extract_locations(result)
            hints = derive_component_hints(purl, locations)
            primary_hint = hints[0]

            findings.append(
                {
                    "cve": rule_id,
                    "severity": normalize_severity(
                        properties.get("cvssV3_severity"),
                        string_list(properties.get("tags")),
                    ),
                    "score": optional_string(properties.get("security-severity")),
                    "package": purl,
                    "package_name": package_name(purl),
                    "fixed_version": optional_string(properties.get("fixed_version")),
                    "help_uri": optional_string(rule.get("helpUri")),
                    "primary_component": primary_hint.name,
                    "primary_component_kind": primary_hint.kind,
                    "primary_evidence_path": primary_hint.evidence_path,
                    "evidence_paths": locations,
                }
            )

    findings.sort(
        key=lambda item: (
            SEVERITY_ORDER.get(item["severity"], SEVERITY_ORDER["UNKNOWN"]),
            item["primary_component"],
            -score_value(item["score"]),
            item["cve"],
        )
    )

    components = build_components(findings)
    severity_counts = compute_severity_counts(findings)
    payload = {
        "service": args.service,
        "sarif_file": str(sarif_file),
        "sarif_present": True,
        "finding_count": len(findings),
        "component_count": len(components),
        "severity_counts": severity_counts,
        "components": components,
    }

    json_output.parent.mkdir(parents=True, exist_ok=True)
    markdown_output.parent.mkdir(parents=True, exist_ok=True)
    json_output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

    markdown_lines = [
        f"# Docker Scout Component Report for `{args.service}`",
        "",
        "Compact component-oriented view of the Docker Scout SARIF. "
        "Each block groups findings by the most likely affected executable or package.",
        "",
        f"- Findings: {len(findings)} | Components: {len(components)}",
        f"- Severity: {severity_count_labels(payload['severity_counts'])}",
        "",
    ]
    for component in components:
        markdown_lines.extend(render_component_block(component))
        markdown_lines.append("")

    markdown_output.write_text("\n".join(markdown_lines).rstrip() + "\n", encoding="utf-8")

    print(f"### Docker Scout component report ({args.service})")
    print("")
    print(f"- Findings: {len(findings)} | Components: {len(components)}")
    print(f"- Severity: {severity_count_labels(payload['severity_counts'])}")
    print("- Artifact files: `component-report.md`, `component-report.json`")
    print("")

    summary_limit = None if args.summary_limit < 0 else args.summary_limit
    visible_components = components if summary_limit is None else components[:summary_limit]
    for component in visible_components:
        print("\n".join(render_component_block(component)))
        print("")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        raise SystemExit(2)
