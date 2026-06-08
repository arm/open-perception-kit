#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import base64
import json
import os
import sys
import time
from collections.abc import Sequence
from pathlib import Path
from typing import Any
from urllib import error, parse, request


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Print Sonar quality gate failures into the CI log.",
    )
    parser.add_argument("--report-task-file", type=Path, required=True)
    parser.add_argument("--branch", default="")
    parser.add_argument("--timeout-seconds", type=int, default=300)
    parser.add_argument("--poll-interval-seconds", type=int, default=5)
    parser.add_argument("--issue-limit", type=int, default=25)
    parser.add_argument("--hotspot-limit", type=int, default=10)
    parser.add_argument("--snippet-context", type=int, default=2)
    parser.add_argument("--probe-api-access", action="store_true")
    return parser.parse_args(argv)


def fail(message: str, exit_code: int = 2) -> int:
    print(message, file=sys.stderr)
    return exit_code


def build_api_url(
    server_url: str,
    api_path: str,
    params: dict[str, str] | None = None,
) -> str:
    base_url = server_url.rstrip("/")
    normalized_path = api_path if api_path.startswith("/") else f"/{api_path}"
    url = f"{base_url}{normalized_path}"
    if params:
        url = f"{url}?{parse.urlencode(params)}"
    return url


def decode_json_response(api_path: str, response_body: str) -> dict[str, Any]:
    try:
        payload = json.loads(response_body)
    except json.JSONDecodeError as exc:
        raise RuntimeError(
            f"Sonar API request failed for {api_path}: invalid JSON response"
        ) from exc

    if not isinstance(payload, dict):
        raise RuntimeError(
            f"Sonar API request failed for {api_path}: expected a JSON object response"
        )
    return payload


def api_get_json(
    server_url: str,
    api_path: str,
    token: str,
    params: dict[str, str] | None = None,
) -> dict[str, Any]:
    url = build_api_url(server_url, api_path, params)

    auth_headers = build_auth_headers(token)

    last_error: RuntimeError | None = None
    for index, (_, auth_header) in enumerate(auth_headers):
        req = request.Request(
            url,
            headers={
                "Authorization": auth_header,
                "Accept": "application/json",
            },
        )

        try:
            with request.urlopen(req, timeout=30) as resp:
                charset = resp.headers.get_content_charset("utf-8")
                body = resp.read().decode(charset)
                return decode_json_response(api_path, body)
        except error.HTTPError as exc:
            body = exc.read().decode("utf-8", errors="replace").strip()
            detail = f"HTTP {exc.code} {exc.reason}"
            if body:
                detail = f"{detail} | {body}"
            last_error = RuntimeError(f"Sonar API request failed for {api_path}: {detail}")
            if exc.code in {401, 403} and index + 1 < len(auth_headers):
                continue
            raise last_error from exc
        except error.URLError as exc:
            raise RuntimeError(f"Failed to reach SonarQube at {server_url}: {exc.reason}") from exc

    if last_error is not None:
        raise last_error

    raise RuntimeError(f"Sonar API request failed for {api_path}: no authentication methods succeeded")


def load_report_task(report_task_file: Path, branch: str) -> dict[str, str]:
    if not report_task_file.exists():
        raise RuntimeError(
            "Sonar analysis did not produce report-task.txt. "
            "The scanner step likely failed before uploading analysis."
        )

    data: dict[str, str] = {}
    for raw_line in report_task_file.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        key, sep, value = line.partition("=")
        if sep:
            data[key.strip()] = value.strip()

    data["branch"] = branch.strip()
    data["serverUrl"] = data.get("serverUrl") or os.environ.get("SONAR_HOST_URL", "").strip()

    missing = [key for key in ("serverUrl", "ceTaskId", "projectKey") if not data.get(key)]
    if missing:
        raise RuntimeError(f"report-task.txt is missing required values: {', '.join(missing)}")

    return data


def build_auth_headers(token: str) -> tuple[tuple[str, str], tuple[str, str]]:
    basic_token = base64.b64encode(f"{token}:".encode("utf-8")).decode("ascii")
    return (
        ("bearer", f"Bearer {token}"),
        ("basic", f"Basic {basic_token}"),
    )


def summarize_probe_body(body: str) -> str:
    compact_body = " ".join(body.split())
    if not compact_body:
        return ""

    try:
        payload = json.loads(compact_body)
    except json.JSONDecodeError:
        return compact_body[:160]

    if isinstance(payload, dict):
        errors = payload.get("errors")
        if isinstance(errors, list):
            messages = []
            for item in errors:
                if not isinstance(item, dict):
                    continue
                message = str(item.get("msg", "")).strip()
                if message:
                    messages.append(message)
            if messages:
                return "; ".join(messages)[:160]

        keys = sorted(str(key) for key in payload.keys())
        if keys:
            return f"keys={', '.join(keys[:6])}"

    return compact_body[:160]


def probe_api_access(
    server_url: str,
    api_path: str,
    token: str,
    params: dict[str, str] | None = None,
) -> list[str]:
    url = build_api_url(server_url, api_path, params)

    results: list[str] = []
    for auth_name, auth_header in build_auth_headers(token):
        req = request.Request(
            url,
            headers={
                "Authorization": auth_header,
                "Accept": "application/json",
            },
        )
        try:
            with request.urlopen(req, timeout=30) as resp:
                charset = resp.headers.get_content_charset("utf-8")
                body = resp.read().decode(charset)
                summary = summarize_probe_body(body)
                result = f"- {api_path} [{auth_name}]: HTTP {resp.status}"
                if summary:
                    result = f"{result} | {summary}"
                results.append(result)
        except error.HTTPError as exc:
            body = exc.read().decode("utf-8", errors="replace")
            summary = summarize_probe_body(body)
            result = f"- {api_path} [{auth_name}]: HTTP {exc.code} {exc.reason}"
            if summary:
                result = f"{result} | {summary}"
            results.append(result)
        except error.URLError as exc:
            results.append(f"- {api_path} [{auth_name}]: URL error | {exc.reason}")

    return results


def wait_for_task(
    server_url: str,
    task_id: str,
    token: str,
    timeout_seconds: int,
    poll_interval_seconds: int,
) -> dict[str, Any]:
    deadline = time.monotonic() + timeout_seconds
    last_status = "PENDING"

    while time.monotonic() <= deadline:
        payload = api_get_json(server_url, "/api/ce/task", token, {"id": task_id})
        task = payload.get("task")
        if not isinstance(task, dict):
            raise RuntimeError("Sonar compute-engine response is missing the task object.")

        last_status = str(task.get("status", "UNKNOWN")).upper()
        if last_status in {"SUCCESS", "FAILED", "CANCELED"}:
            return task
        time.sleep(max(1, poll_interval_seconds))

    raise RuntimeError(
        f"Timed out waiting for Sonar compute-engine task {task_id} "
        f"(last status: {last_status})."
    )


def short_component(project_key: str, component: str) -> str:
    prefix = f"{project_key}:"
    if component.startswith(prefix):
        return component[len(prefix):]
    return component


def build_query(branch: str, **params: str) -> dict[str, str]:
    query = {key: value for key, value in params.items() if value}
    if branch:
        query["branch"] = branch
    return query


def extract_line_number(entry: dict[str, Any]) -> int | None:
    line = entry.get("line")
    if isinstance(line, int):
        return line
    if isinstance(line, str) and line.isdigit():
        return int(line)

    text_range = entry.get("textRange")
    if isinstance(text_range, dict):
        start_line = text_range.get("startLine")
        if isinstance(start_line, int):
            return start_line
        if isinstance(start_line, str) and start_line.isdigit():
            return int(start_line)
    return None


def format_location(project_key: str, component: str, line_number: int | None) -> str:
    short_path = short_component(project_key, component.strip())
    if not short_path:
        return "unknown"
    if line_number is None:
        return short_path
    return f"{short_path}:{line_number}"


def render_component_snippet(
    workspace_root: Path,
    project_key: str,
    component: str,
    line_number: int | None,
    context_lines: int,
) -> list[str]:
    if line_number is None or line_number < 1:
        return []

    relative_path = short_component(project_key, component.strip())
    if not relative_path:
        return []

    workspace_root_resolved = workspace_root.resolve()
    path = (workspace_root / relative_path).resolve(strict=False)
    try:
        path.relative_to(workspace_root_resolved)
    except ValueError:
        return []
    if not path.is_file():
        return []

    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeDecodeError):
        return []

    if line_number > len(lines):
        return []

    start = max(1, line_number - max(0, context_lines))
    end = min(len(lines), line_number + max(0, context_lines))
    width = len(str(end))
    snippet: list[str] = []
    for current_line in range(start, end + 1):
        marker = ">" if current_line == line_number else " "
        snippet.append(f"    {marker} {current_line:>{width}} | {lines[current_line - 1]}")
    return snippet


def print_finding(
    header: str,
    rule_key: str,
    rule_name: str,
    message: str,
    snippet: Sequence[str],
    effort: str = "",
) -> None:
    print(header)
    if rule_name:
        print(f"  Rule: {rule_key} - {rule_name}")
    else:
        print(f"  Rule: {rule_key}")
    print(f"  Message: {message}")
    if effort:
        print(f"  Effort: {effort}")
    if snippet:
        print("  Snippet:")
        for snippet_line in snippet:
            print(snippet_line)


def lookup_rule_name(
    server_url: str,
    token: str,
    rule_key: str,
    rule_cache: dict[str, str],
) -> str:
    cached = rule_cache.get(rule_key)
    if cached is not None:
        return cached

    try:
        payload = api_get_json(server_url, "/api/rules/show", token, {"key": rule_key})
    except RuntimeError:
        rule_cache[rule_key] = ""
        return ""

    rule = payload.get("rule")
    if isinstance(rule, dict):
        name = str(rule.get("name", "")).strip()
        rule_cache[rule_key] = name
        return name

    rule_cache[rule_key] = ""
    return ""


def print_failed_conditions(conditions: Sequence[dict[str, Any]]) -> None:
    failed_conditions = [
        condition
        for condition in conditions
        if isinstance(condition, dict)
        and str(condition.get("status", "")).upper() in {"ERROR", "WARN"}
    ]
    if not failed_conditions:
        return

    print("Failed conditions:")
    for condition in failed_conditions:
        metric = str(condition.get("metricKey", "")).strip() or "unknown"
        actual = str(condition.get("actualValue", "n/a")).strip() or "n/a"
        comparator = str(condition.get("comparator", "")).strip() or "?"
        threshold = str(condition.get("errorThreshold", "n/a")).strip() or "n/a"
        print(f"- {metric}: actual {actual} {comparator} threshold {threshold}")
    print()


def collect_secondary_locations(
    issue: dict[str, Any],
    project_key: str,
    limit: int = 3,
) -> list[str]:
    flows = issue.get("flows")
    if not isinstance(flows, list):
        return []

    secondary_locations: list[str] = []
    for flow in flows:
        if not isinstance(flow, dict):
            continue
        locations = flow.get("locations")
        if not isinstance(locations, list):
            continue

        for location in locations:
            if not isinstance(location, dict):
                continue
            component = str(location.get("component", "")).strip()
            message = str(location.get("msg", "")).strip()
            line_number = extract_line_number(location)
            display_location = format_location(project_key, component, line_number)
            if message and message != "+1":
                secondary_locations.append(f"{display_location} {message}")
            else:
                secondary_locations.append(display_location)
            if len(secondary_locations) >= limit:
                return secondary_locations

    return secondary_locations


def print_issues(
    issue_payload: dict[str, Any],
    project_key: str,
    workspace_root: Path,
    server_url: str,
    token: str,
    issue_limit: int,
    snippet_context: int,
    rule_cache: dict[str, str],
) -> None:
    issues = issue_payload.get("issues")
    if not isinstance(issues, list):
        return

    total = int(issue_payload.get("total", len(issues)) or 0)
    if not issues:
        print("Issue snapshot: no open issues were returned for the new-code period.")
        print()
        return

    print(f"Issue snapshot (new code, unresolved): total={total}, shown={len(issues)}")
    for issue in issues:
        issue_type = str(issue.get("type", "ISSUE")).strip() or "ISSUE"
        severity = str(issue.get("severity", "UNKNOWN")).strip() or "UNKNOWN"
        rule_key = str(issue.get("rule", "")).strip() or "unknown-rule"
        rule_name = lookup_rule_name(server_url, token, rule_key, rule_cache)
        component = str(issue.get("component", "")).strip()
        line_number = extract_line_number(issue)
        location = format_location(project_key, component, line_number)
        message = str(issue.get("message", "")).strip() or "No message provided."
        effort = str(issue.get("effort", "")).strip()

        snippet = render_component_snippet(
            workspace_root,
            project_key,
            component,
            line_number,
            snippet_context,
        )
        print_finding(
            f"- {issue_type} {severity} {location}",
            rule_key,
            rule_name,
            message,
            snippet,
            effort,
        )

        secondary_locations = collect_secondary_locations(issue, project_key)
        if secondary_locations:
            print("  Secondary locations:")
            for secondary in secondary_locations:
                print(f"  - {secondary}")
        print()

    if total > issue_limit:
        print(f"- ... truncated after {issue_limit} issues")
        print()


def print_hotspots(
    hotspot_payload: dict[str, Any],
    project_key: str,
    workspace_root: Path,
    server_url: str,
    token: str,
    hotspot_limit: int,
    snippet_context: int,
    rule_cache: dict[str, str],
) -> None:
    hotspots = hotspot_payload.get("hotspots")
    if not isinstance(hotspots, list):
        return

    paging = hotspot_payload.get("paging")
    paging_total: Any = len(hotspots)
    if isinstance(paging, dict):
        paging_total = paging.get("total", len(hotspots))
    total = int(paging_total or len(hotspots))
    if not hotspots:
        return

    print(f"Security hotspots requiring review (new code): total={total}, shown={len(hotspots)}")
    for hotspot in hotspots:
        component = str(hotspot.get("component", "")).strip()
        line_number = extract_line_number(hotspot)
        location = format_location(project_key, component, line_number)
        probability = str(hotspot.get("vulnerabilityProbability", "UNKNOWN")).strip() or "UNKNOWN"
        status = str(hotspot.get("status", "UNKNOWN")).strip() or "UNKNOWN"
        rule_key = (
            str(hotspot.get("ruleKey", "")).strip()
            or str(hotspot.get("rule", "")).strip()
            or "unknown-rule"
        )
        rule_name = lookup_rule_name(server_url, token, rule_key, rule_cache)
        message = str(hotspot.get("message", "")).strip() or "No message provided."

        snippet = render_component_snippet(
            workspace_root,
            project_key,
            component,
            line_number,
            snippet_context,
        )
        print_finding(
            f"- {probability} {status} {location}",
            rule_key,
            rule_name,
            message,
            snippet,
        )
        print()

    if total > hotspot_limit:
        print(f"- ... truncated after {hotspot_limit} hotspots")
        print()


def print_api_access_probe(
    ctx: dict[str, str],
    analysis_id: str,
    token: str,
    issue_limit: int,
    hotspot_limit: int,
) -> None:
    print("Sonar API access probe")
    print(f"Project: {ctx['projectKey']}")
    if ctx["branch"]:
        print(f"Branch: {ctx['branch']}")
    print()

    probe_targets = (
        (
            "/api/ce/task",
            {"id": ctx["ceTaskId"]},
        ),
        (
            "/api/qualitygates/project_status",
            {"analysisId": analysis_id},
        ),
        (
            "/api/issues/search",
            build_query(
                ctx["branch"],
                componentKeys=ctx["projectKey"],
                resolved="false",
                inNewCodePeriod="true",
                ps=str(max(1, issue_limit)),
            ),
        ),
        (
            "/api/hotspots/search",
            build_query(
                ctx["branch"],
                projectKey=ctx["projectKey"],
                status="TO_REVIEW",
                inNewCodePeriod="true",
                ps=str(max(1, hotspot_limit)),
            ),
        ),
    )

    for api_path, params in probe_targets:
        for result in probe_api_access(ctx["serverUrl"], api_path, token, params):
            print(result)
    print()


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    token = os.environ.get("SONAR_TOKEN", "").strip()
    if not token:
        return fail("Required environment variable is missing: SONAR_TOKEN")

    try:
        ctx = load_report_task(args.report_task_file, args.branch)
        task = wait_for_task(
            ctx["serverUrl"],
            ctx["ceTaskId"],
            token,
            timeout_seconds=args.timeout_seconds,
            poll_interval_seconds=args.poll_interval_seconds,
        )
    except RuntimeError as exc:
        return fail(f"Sonar report error: {exc}")

    workspace_root = args.report_task_file.resolve().parent.parent
    rule_cache: dict[str, str] = {}

    print("Sonar quality gate report")
    print(f"Project: {ctx['projectKey']}")
    if ctx["branch"]:
        print(f"Branch: {ctx['branch']}")
    if ctx.get("dashboardUrl"):
        print(f"Dashboard: {ctx['dashboardUrl']}")
    print(f"CE task id: {ctx['ceTaskId']}")
    print()

    task_status = str(task.get("status", "UNKNOWN")).upper()
    print(f"Compute-engine status: {task_status}")
    if task_status != "SUCCESS":
        error_message = str(task.get("errorMessage", "")).strip() or "No error message provided."
        print(f"Compute-engine failure message: {error_message}")
        return 1

    analysis_id = str(task.get("analysisId", "")).strip()
    if not analysis_id:
        return fail("Sonar report error: compute-engine task completed without an analysisId.")

    if args.probe_api_access:
        print_api_access_probe(
            ctx,
            analysis_id,
            token,
            args.issue_limit,
            args.hotspot_limit,
        )
        return 0

    try:
        quality_gate = api_get_json(
            ctx["serverUrl"],
            "/api/qualitygates/project_status",
            token,
            {"analysisId": analysis_id},
        )
    except RuntimeError as exc:
        return fail(f"Sonar report error: {exc}")

    project_status = quality_gate.get("projectStatus")
    if not isinstance(project_status, dict):
        return fail("Sonar report error: quality gate response is missing projectStatus.")

    gate_status = str(project_status.get("status", "UNKNOWN")).upper()
    print(f"Quality gate status: {gate_status}")
    print()

    conditions = project_status.get("conditions")
    if isinstance(conditions, list):
        print_failed_conditions(conditions)

    if gate_status == "OK":
        print("Sonar quality gate passed.")
        return 0

    try:
        hotspots = api_get_json(
            ctx["serverUrl"],
            "/api/hotspots/search",
            token,
            build_query(
                ctx["branch"],
                projectKey=ctx["projectKey"],
                status="TO_REVIEW",
                inNewCodePeriod="true",
                ps=str(max(1, args.hotspot_limit)),
            ),
        )
        print_hotspots(
            hotspots,
            ctx["projectKey"],
            workspace_root,
            ctx["serverUrl"],
            token,
            args.hotspot_limit,
            args.snippet_context,
            rule_cache,
        )
    except RuntimeError as exc:
        print(f"Security hotspot snapshot unavailable: {exc}")
        print()

    try:
        issues = api_get_json(
            ctx["serverUrl"],
            "/api/issues/search",
            token,
            build_query(
                ctx["branch"],
                componentKeys=ctx["projectKey"],
                resolved="false",
                inNewCodePeriod="true",
                ps=str(max(1, args.issue_limit)),
                s="FILE_LINE",
                asc="true",
            ),
        )
    except RuntimeError as exc:
        print(f"Issue snapshot unavailable: {exc}")
        print()
        return 1

    print_issues(
        issues,
        ctx["projectKey"],
        workspace_root,
        ctx["serverUrl"],
        token,
        args.issue_limit,
        args.snippet_context,
        rule_cache,
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
