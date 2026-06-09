#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import base64
import json
import logging
import os
import sys
import time
from collections.abc import Callable, Sequence
from enum import IntEnum
from pathlib import Path
from typing import Any, TypedDict, cast
from urllib import error, parse, request


JsonObject = dict[str, Any]
HTTP_RETRY_ATTEMPTS = 3
HTTP_RETRY_INITIAL_DELAY_SECONDS = 1.0
HTTP_RETRYABLE_STATUS_CODES = frozenset({408, 429, 500, 502, 503, 504})
LOGGER = logging.getLogger("sonar_quality_gate_report")


class ExitCode(IntEnum):
    OK = 0
    QUALITY_GATE_FAILED = 1
    SCRIPT_ERROR = 2
    EXECUTION_ERROR = 3


class ReportTaskContext(TypedDict, total=False):
    branch: str
    ceTaskId: str
    dashboardUrl: str
    projectKey: str
    serverUrl: str


class TextRangePayload(TypedDict, total=False):
    startLine: int | str


class IssueLocationPayload(TypedDict, total=False):
    component: str
    line: int | str
    msg: str
    textRange: TextRangePayload


class IssueFlowPayload(TypedDict, total=False):
    locations: list[IssueLocationPayload]


class IssuePayload(TypedDict, total=False):
    component: str
    effort: str
    flows: list[IssueFlowPayload]
    line: int | str
    message: str
    rule: str
    severity: str
    textRange: TextRangePayload
    type: str


class IssuesSearchPayload(TypedDict, total=False):
    issues: list[IssuePayload]
    total: int


class HotspotPayload(TypedDict, total=False):
    component: str
    line: int | str
    message: str
    rule: str
    ruleKey: str
    status: str
    textRange: TextRangePayload
    vulnerabilityProbability: str


class PagingPayload(TypedDict, total=False):
    total: int


class HotspotsSearchPayload(TypedDict, total=False):
    hotspots: list[HotspotPayload]
    paging: PagingPayload


class QualityGateCondition(TypedDict, total=False):
    actualValue: str
    comparator: str
    errorThreshold: str
    metricKey: str
    status: str


class ProjectStatusPayload(TypedDict, total=False):
    conditions: list[QualityGateCondition]
    status: str


class QualityGatePayload(TypedDict, total=False):
    projectStatus: ProjectStatusPayload


class ComputeEngineTaskPayload(TypedDict, total=False):
    analysisId: str
    errorMessage: str
    status: str


class ComputeEnginePayload(TypedDict, total=False):
    task: ComputeEngineTaskPayload


class RulePayload(TypedDict, total=False):
    name: str


class RuleShowPayload(TypedDict, total=False):
    rule: RulePayload


class ProbeErrorPayload(TypedDict, total=False):
    msg: str


class ProbeBodyPayload(TypedDict, total=False):
    errors: list[ProbeErrorPayload]


class HttpTextResponse(TypedDict):
    body: str
    status: int


class HttpRequestError(RuntimeError):
    def __init__(
        self,
        api_path: str,
        *,
        status_code: int | None = None,
        reason: str = "",
        body: str = "",
        is_url_error: bool = False,
    ) -> None:
        self.api_path = api_path
        self.status_code = status_code
        self.reason = reason
        self.body = body
        self.is_url_error = is_url_error
        super().__init__(api_path)


def configure_logging() -> None:
    if LOGGER.handlers:
        return

    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(logging.Formatter("%(message)s"))
    LOGGER.addHandler(handler)
    LOGGER.setLevel(logging.INFO)
    LOGGER.propagate = False


def log_info(message: str = "") -> None:
    LOGGER.info(message)


def log_warning(message: str) -> None:
    LOGGER.warning(message)


def log_error(message: str) -> None:
    LOGGER.error(message)


def retry_delay_seconds(attempt_index: int) -> float:
    return HTTP_RETRY_INITIAL_DELAY_SECONDS * (2**attempt_index)


def format_http_request_error(exc: HttpRequestError) -> str:
    if exc.is_url_error:
        return f"URL error | {exc.reason}"

    detail = f"HTTP {exc.status_code} {exc.reason}".strip()
    summary = summarize_probe_body(exc.body)
    if summary:
        return f"{detail} | {summary}"
    return detail


def open_text_response(request_obj: request.Request) -> HttpTextResponse:
    with request.urlopen(request_obj, timeout=30) as resp:
        charset = resp.headers.get_content_charset("utf-8")
        return {
            "body": resp.read().decode(charset),
            "status": int(resp.status),
        }


def request_text_with_retry(
    api_path: str,
    request_factory: Callable[[], request.Request],
) -> HttpTextResponse:
    last_error: HttpRequestError | None = None

    for attempt_index in range(HTTP_RETRY_ATTEMPTS):
        try:
            return open_text_response(request_factory())
        except error.HTTPError as exc:
            body = exc.read().decode("utf-8", errors="replace").strip()
            last_error = HttpRequestError(
                api_path,
                status_code=exc.code,
                reason=exc.reason,
                body=body,
            )
            if (
                exc.code in HTTP_RETRYABLE_STATUS_CODES
                and attempt_index + 1 < HTTP_RETRY_ATTEMPTS
            ):
                log_warning(
                    f"Retrying Sonar API request for {api_path} "
                    f"({attempt_index + 2}/{HTTP_RETRY_ATTEMPTS}) after "
                    f"{format_http_request_error(last_error)}"
                )
                time.sleep(retry_delay_seconds(attempt_index))
                continue
            raise last_error from exc
        except error.URLError as exc:
            last_error = HttpRequestError(
                api_path,
                reason=str(exc.reason),
                is_url_error=True,
            )
            if attempt_index + 1 < HTTP_RETRY_ATTEMPTS:
                log_warning(
                    f"Retrying Sonar API request for {api_path} "
                    f"({attempt_index + 2}/{HTTP_RETRY_ATTEMPTS}) after "
                    f"{format_http_request_error(last_error)}"
                )
                time.sleep(retry_delay_seconds(attempt_index))
                continue
            raise last_error from exc

    if last_error is not None:
        raise last_error

    raise HttpRequestError(api_path, reason="request failed without an error object")


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


def fail(message: str, exit_code: ExitCode = ExitCode.SCRIPT_ERROR) -> int:
    log_error(message)
    return int(exit_code)


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
) -> JsonObject:
    url = build_api_url(server_url, api_path, params)

    auth_headers = build_auth_headers(token)

    last_error: RuntimeError | None = None
    for index, (_, auth_header) in enumerate(auth_headers):
        try:
            response = request_text_with_retry(
                api_path,
                lambda auth_header=auth_header: request.Request(
                    url,
                    headers={
                        "Authorization": auth_header,
                        "Accept": "application/json",
                    },
                ),
            )
            return decode_json_response(api_path, response["body"])
        except HttpRequestError as exc:
            last_error = RuntimeError(
                f"Sonar API request failed for {api_path}: {format_http_request_error(exc)}"
            )
            if exc.status_code in {401, 403} and index + 1 < len(auth_headers):
                continue
            raise last_error from exc

    if last_error is not None:
        raise last_error

    raise RuntimeError(f"Sonar API request failed for {api_path}: no authentication methods succeeded")


def load_report_task(report_task_file: Path, branch: str) -> ReportTaskContext:
    if not report_task_file.exists():
        raise RuntimeError(
            "Sonar analysis did not produce report-task.txt. "
            "The scanner step likely failed before uploading analysis."
        )

    try:
        report_task_lines = report_task_file.read_text(encoding="utf-8").splitlines()
    except UnicodeDecodeError as exc:
        raise RuntimeError(
            f"Failed to decode report-task.txt as UTF-8: {report_task_file}"
        ) from exc
    except OSError as exc:
        raise RuntimeError(f"Failed to read report-task.txt: {report_task_file}") from exc

    raw_data: dict[str, str] = {}
    for raw_line in report_task_lines:
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        key, sep, value = line.partition("=")
        if sep:
            raw_data[key.strip()] = value.strip()

    server_url = raw_data.get("serverUrl") or os.environ.get("SONAR_HOST_URL", "").strip()
    missing = [
        key
        for key, value in (
            ("serverUrl", server_url),
            ("ceTaskId", raw_data.get("ceTaskId", "")),
            ("projectKey", raw_data.get("projectKey", "")),
        )
        if not value
    ]
    if missing:
        raise RuntimeError(f"report-task.txt is missing required values: {', '.join(missing)}")

    data: ReportTaskContext = {
        "branch": branch.strip(),
        "ceTaskId": raw_data["ceTaskId"],
        "projectKey": raw_data["projectKey"],
        "serverUrl": server_url,
    }
    dashboard_url = raw_data.get("dashboardUrl", "").strip()
    if dashboard_url:
        data["dashboardUrl"] = dashboard_url
    return data


def build_auth_headers(token: str) -> tuple[tuple[str, str], tuple[str, str]]:
    basic_token = base64.b64encode(f"{token}:".encode("utf-8")).decode("ascii")
    return (
        ("basic", f"Basic {basic_token}"),
        ("bearer", f"Bearer {token}"),
    )


def summarize_probe_errors(errors: Any) -> str:
    if not isinstance(errors, list):
        return ""

    messages: list[str] = []
    for item in errors:
        if not isinstance(item, dict):
            continue
        message = str(item.get("msg", "")).strip()
        if message:
            messages.append(message)
    return "; ".join(messages)[:160] if messages else ""


def summarize_probe_payload(payload: Any, fallback: str) -> str:
    if not isinstance(payload, dict):
        return fallback[:160]

    probe_payload = cast(ProbeBodyPayload, payload)
    error_summary = summarize_probe_errors(probe_payload.get("errors"))
    if error_summary:
        return error_summary

    keys = sorted(str(key) for key in payload.keys())
    if keys:
        return f"keys={', '.join(keys[:6])}"
    return fallback[:160]


def summarize_probe_body(body: str) -> str:
    compact_body = " ".join(body.split())
    if not compact_body:
        return ""

    try:
        payload = json.loads(compact_body)
    except json.JSONDecodeError:
        return compact_body[:160]

    return summarize_probe_payload(payload, compact_body)


def probe_api_access(
    server_url: str,
    api_path: str,
    token: str,
    params: dict[str, str] | None = None,
) -> list[str]:
    url = build_api_url(server_url, api_path, params)

    results: list[str] = []
    for auth_name, auth_header in build_auth_headers(token):
        try:
            response = request_text_with_retry(
                api_path,
                lambda auth_header=auth_header: request.Request(
                    url,
                    headers={
                        "Authorization": auth_header,
                        "Accept": "application/json",
                    },
                ),
            )
            summary = summarize_probe_body(response["body"])
            result = f"- {api_path} [{auth_name}]: HTTP {response['status']}"
            if summary:
                result = f"{result} | {summary}"
            results.append(result)
        except HttpRequestError as exc:
            result = f"- {api_path} [{auth_name}]: {format_http_request_error(exc)}"
            results.append(result)

    return results


def wait_for_task(
    server_url: str,
    task_id: str,
    token: str,
    timeout_seconds: int,
    poll_interval_seconds: int,
) -> ComputeEngineTaskPayload:
    deadline = time.monotonic() + timeout_seconds
    last_status = "PENDING"

    while time.monotonic() <= deadline:
        payload = cast(
            ComputeEnginePayload,
            api_get_json(server_url, "/api/ce/task", token, {"id": task_id}),
        )
        task = payload.get("task")
        if not isinstance(task, dict):
            raise RuntimeError("Sonar compute-engine response is missing the task object.")

        task_payload = cast(ComputeEngineTaskPayload, task)
        last_status = str(task_payload.get("status", "UNKNOWN")).upper()
        if last_status in {"SUCCESS", "FAILED", "CANCELED"}:
            return task_payload
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


def extract_line_number(
    entry: IssuePayload | IssueLocationPayload | HotspotPayload,
) -> int | None:
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
    log_info(header)
    if rule_name:
        log_info(f"  Rule: {rule_key} - {rule_name}")
    else:
        log_info(f"  Rule: {rule_key}")
    log_info(f"  Message: {message}")
    if effort:
        log_info(f"  Effort: {effort}")
    if snippet:
        log_info("  Snippet:")
        for snippet_line in snippet:
            log_info(snippet_line)


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
        payload = cast(
            RuleShowPayload,
            api_get_json(server_url, "/api/rules/show", token, {"key": rule_key}),
        )
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


def print_failed_conditions(conditions: Sequence[QualityGateCondition]) -> None:
    failed_conditions = [
        condition
        for condition in conditions
        if isinstance(condition, dict)
        and str(condition.get("status", "")).upper() in {"ERROR", "WARN"}
    ]
    if not failed_conditions:
        return

    log_info("Failed conditions:")
    for condition in failed_conditions:
        metric = str(condition.get("metricKey", "")).strip() or "unknown"
        actual = str(condition.get("actualValue", "n/a")).strip() or "n/a"
        comparator = str(condition.get("comparator", "")).strip() or "?"
        threshold = str(condition.get("errorThreshold", "n/a")).strip() or "n/a"
        log_info(f"- {metric}: actual {actual} {comparator} threshold {threshold}")
    log_info()


def format_secondary_location(location: Any, project_key: str) -> str | None:
    if not isinstance(location, dict):
        return None

    component = str(location.get("component", "")).strip()
    message = str(location.get("msg", "")).strip()
    line_number = extract_line_number(location)
    display_location = format_location(project_key, component, line_number)
    if message and message != "+1":
        return f"{display_location} {message}"
    return display_location


def collect_flow_secondary_locations(flow: Any, project_key: str, limit: int) -> list[str]:
    if limit <= 0 or not isinstance(flow, dict):
        return []

    locations = flow.get("locations")
    if not isinstance(locations, list):
        return []

    secondary_locations: list[str] = []
    for location in locations:
        display_location = format_secondary_location(location, project_key)
        if display_location is None:
            continue
        secondary_locations.append(display_location)
        if len(secondary_locations) >= limit:
            break
    return secondary_locations


def collect_secondary_locations(
    issue: IssuePayload,
    project_key: str,
    limit: int = 3,
) -> list[str]:
    flows = issue.get("flows")
    if not isinstance(flows, list):
        return []

    secondary_locations: list[str] = []
    for flow in flows:
        remaining = limit - len(secondary_locations)
        secondary_locations.extend(collect_flow_secondary_locations(flow, project_key, remaining))
        if len(secondary_locations) >= limit:
            return secondary_locations

    return secondary_locations


def print_issues(
    issue_payload: IssuesSearchPayload,
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
        log_info("Issue snapshot: no open issues were returned for the new-code period.")
        log_info()
        return

    log_info(f"Issue snapshot (new code, unresolved): total={total}, shown={len(issues)}")
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
            log_info("  Secondary locations:")
            for secondary in secondary_locations:
                log_info(f"  - {secondary}")
        log_info()

    if total > issue_limit:
        log_info(f"- ... truncated after {issue_limit} issues")
        log_info()


def print_hotspots(
    hotspot_payload: HotspotsSearchPayload,
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

    log_info(f"Security hotspots requiring review (new code): total={total}, shown={len(hotspots)}")
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
        log_info()

    if total > hotspot_limit:
        log_info(f"- ... truncated after {hotspot_limit} hotspots")
        log_info()


def load_issue_snapshot(
    ctx: ReportTaskContext,
    token: str,
    issue_limit: int,
) -> IssuesSearchPayload:
    return cast(
        IssuesSearchPayload,
        api_get_json(
            ctx["serverUrl"],
            "/api/issues/search",
            token,
            build_query(
                ctx["branch"],
                componentKeys=ctx["projectKey"],
                resolved="false",
                inNewCodePeriod="true",
                ps=str(max(1, issue_limit)),
                s="FILE_LINE",
                asc="true",
            ),
        ),
    )


def print_api_access_probe(
    ctx: ReportTaskContext,
    analysis_id: str,
    token: str,
    issue_limit: int,
    hotspot_limit: int,
) -> None:
    log_info("Sonar API access probe")
    log_info(f"Project: {ctx['projectKey']}")
    if ctx["branch"]:
        log_info(f"Branch: {ctx['branch']}")
    log_info()

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
            log_info(result)
    log_info()


def load_compute_engine_task(
    args: argparse.Namespace,
    token: str,
) -> tuple[ReportTaskContext, ComputeEngineTaskPayload] | int:
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

    return ctx, task


def print_report_header(ctx: ReportTaskContext) -> None:
    log_info("Sonar quality gate report")
    log_info(f"Project: {ctx['projectKey']}")
    if ctx["branch"]:
        log_info(f"Branch: {ctx['branch']}")
    if ctx.get("dashboardUrl"):
        log_info(f"Dashboard: {ctx['dashboardUrl']}")
    log_info(f"CE task id: {ctx['ceTaskId']}")
    log_info()


def extract_analysis_id(task: ComputeEngineTaskPayload) -> str | int:
    task_status = str(task.get("status", "UNKNOWN")).upper()
    log_info(f"Compute-engine status: {task_status}")
    if task_status != "SUCCESS":
        error_message = str(task.get("errorMessage", "")).strip() or "No error message provided."
        log_error(f"Compute-engine failure message: {error_message}")
        return int(ExitCode.EXECUTION_ERROR)

    analysis_id = str(task.get("analysisId", "")).strip()
    if not analysis_id:
        return fail("Sonar report error: compute-engine task completed without an analysisId.")
    return analysis_id


def load_quality_gate(
    ctx: ReportTaskContext,
    token: str,
    analysis_id: str,
) -> QualityGatePayload | int:
    try:
        return cast(
            QualityGatePayload,
            api_get_json(
                ctx["serverUrl"],
                "/api/qualitygates/project_status",
                token,
                {"analysisId": analysis_id},
            ),
        )
    except RuntimeError as exc:
        return fail(f"Sonar report error: {exc}")


def print_issue_snapshot_or_error(
    ctx: ReportTaskContext,
    token: str,
    workspace_root: Path,
    gate_status: str,
    issue_limit: int,
    snippet_context: int,
    rule_cache: dict[str, str],
) -> int | None:
    try:
        issues = load_issue_snapshot(ctx, token, issue_limit)
    except RuntimeError as exc:
        if gate_status == "OK":
            log_warning(f"Issue snapshot unavailable: {exc}")
            log_info()
            log_info("Sonar quality gate passed.")
            return int(ExitCode.OK)
        log_error(f"Issue snapshot unavailable: {exc}")
        log_info()
        return int(ExitCode.SCRIPT_ERROR)

    print_issues(
        issues,
        ctx["projectKey"],
        workspace_root,
        ctx["serverUrl"],
        token,
        issue_limit,
        snippet_context,
        rule_cache,
    )
    return None


def print_hotspot_snapshot_or_warning(
    ctx: ReportTaskContext,
    token: str,
    workspace_root: Path,
    hotspot_limit: int,
    snippet_context: int,
    rule_cache: dict[str, str],
) -> None:
    try:
        hotspots = cast(
            HotspotsSearchPayload,
            api_get_json(
                ctx["serverUrl"],
                "/api/hotspots/search",
                token,
                build_query(
                    ctx["branch"],
                    projectKey=ctx["projectKey"],
                    status="TO_REVIEW",
                    inNewCodePeriod="true",
                    ps=str(max(1, hotspot_limit)),
                ),
            ),
        )
        print_hotspots(
            hotspots,
            ctx["projectKey"],
            workspace_root,
            ctx["serverUrl"],
            token,
            hotspot_limit,
            snippet_context,
            rule_cache,
        )
    except RuntimeError as exc:
        log_warning(f"Security hotspot snapshot unavailable: {exc}")
        log_info()


def report_quality_gate(
    args: argparse.Namespace,
    ctx: ReportTaskContext,
    token: str,
    workspace_root: Path,
    analysis_id: str,
    rule_cache: dict[str, str],
) -> int:
    quality_gate = load_quality_gate(ctx, token, analysis_id)
    if isinstance(quality_gate, int):
        return quality_gate

    project_status = quality_gate.get("projectStatus")
    if not isinstance(project_status, dict):
        return fail("Sonar report error: quality gate response is missing projectStatus.")

    gate_status = str(project_status.get("status", "UNKNOWN")).upper()
    log_info(f"Quality gate status: {gate_status}")
    log_info()

    conditions = project_status.get("conditions")
    if isinstance(conditions, list):
        print_failed_conditions(cast(list[QualityGateCondition], conditions))

    issue_error = print_issue_snapshot_or_error(
        ctx,
        token,
        workspace_root,
        gate_status,
        args.issue_limit,
        args.snippet_context,
        rule_cache,
    )
    if issue_error is not None:
        return issue_error

    if gate_status == "OK":
        log_info("Sonar quality gate passed.")
        return int(ExitCode.OK)

    print_hotspot_snapshot_or_warning(
        ctx,
        token,
        workspace_root,
        args.hotspot_limit,
        args.snippet_context,
        rule_cache,
    )
    return int(ExitCode.QUALITY_GATE_FAILED)


def main(argv: Sequence[str] | None = None) -> int:
    configure_logging()
    args = parse_args(argv)
    token = os.environ.get("SONAR_TOKEN", "").strip()
    if not token:
        return fail("Required environment variable is missing: SONAR_TOKEN")

    loaded_task = load_compute_engine_task(args, token)
    if isinstance(loaded_task, int):
        return loaded_task
    ctx, task = loaded_task

    workspace_root = args.report_task_file.resolve().parent.parent
    rule_cache: dict[str, str] = {}

    print_report_header(ctx)
    analysis_id = extract_analysis_id(task)
    if isinstance(analysis_id, int):
        return analysis_id

    if args.probe_api_access:
        print_api_access_probe(
            ctx,
            analysis_id,
            token,
            args.issue_limit,
            args.hotspot_limit,
        )
        return int(ExitCode.OK)

    return report_quality_gate(args, ctx, token, workspace_root, analysis_id, rule_cache)


if __name__ == "__main__":
    configure_logging()
    raise SystemExit(main())
