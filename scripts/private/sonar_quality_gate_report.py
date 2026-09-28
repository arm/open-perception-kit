#!/usr/bin/env python3
################################################################
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
################################################################

from __future__ import annotations

import argparse
import base64
import json
import logging
import os
import subprocess
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
CI_SUPPRESSION_FILE = "ci-suppressions.txt"
CI_SUPPRESSION_CONDITIONS = {
    "UNIT_TEST_COVERAGE": frozenset({"new_coverage"}),
    "CODE_DUPLICATION": frozenset({"new_duplicated_lines_density"}),
    "MAINTAINABILITY": frozenset({"new_maintainability_rating"}),
    "RELIABILITY": frozenset({"new_reliability_rating"}),
    "SECURITY": frozenset({"new_security_rating"}),
    "SECURITY_HOTSPOTS": frozenset({"new_security_hotspots_reviewed"}),
}


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
    pullRequest: str
    pullRequestBase: str
    pullRequestBranch: str
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


class ApiErrorPayload(TypedDict, total=False):
    msg: str


class ApiErrorBodyPayload(TypedDict, total=False):
    errors: list[ApiErrorPayload]


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
    summary = summarize_response_body(exc.body)
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
    parser.add_argument("--pull-request-key", default="")
    parser.add_argument("--pull-request-branch", default="")
    parser.add_argument("--pull-request-base", default="")
    parser.add_argument("--pull-request-base-sha", default="")
    parser.add_argument("--timeout-seconds", type=int, default=300)
    parser.add_argument("--poll-interval-seconds", type=int, default=5)
    parser.add_argument("--issue-limit", type=int, default=25)
    parser.add_argument("--hotspot-limit", type=int, default=10)
    parser.add_argument("--snippet-context", type=int, default=2)
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


def load_report_task(
    report_task_file: Path,
    branch: str,
    pull_request_key: str,
    pull_request_branch: str,
    pull_request_base: str,
) -> ReportTaskContext:
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
    pull_request = pull_request_key.strip()
    if pull_request:
        data["pullRequest"] = pull_request
        pr_branch = pull_request_branch.strip()
        pr_base = pull_request_base.strip()
        if pr_branch:
            data["pullRequestBranch"] = pr_branch
        if pr_base:
            data["pullRequestBase"] = pr_base
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


def parse_response_payload(body: str) -> tuple[str, JsonObject | None]:
    compact_body = " ".join(body.split())
    if not compact_body:
        return "", None

    try:
        payload = json.loads(compact_body)
    except json.JSONDecodeError:
        return compact_body, None

    if not isinstance(payload, dict):
        return compact_body, None
    return compact_body, payload


def api_error_messages(payload: ApiErrorBodyPayload) -> list[str]:
    errors = payload.get("errors")
    if not isinstance(errors, list):
        return []

    messages: list[str] = []
    for item in errors:
        if not isinstance(item, dict):
            continue
        message = str(item.get("msg", "")).strip()
        if message:
            messages.append(message)
    return messages


def summarize_response_payload(payload: JsonObject) -> str:
    messages = api_error_messages(cast(ApiErrorBodyPayload, payload))
    if messages:
        return "; ".join(messages)[:160]

    keys = sorted(str(key) for key in payload.keys())
    if keys:
        return f"keys={', '.join(keys[:6])}"
    return ""


def summarize_response_body(body: str) -> str:
    compact_body, payload = parse_response_payload(body)
    if not compact_body:
        return ""

    if payload is None:
        return compact_body[:160]

    payload_summary = summarize_response_payload(payload)
    if payload_summary:
        return payload_summary
    return compact_body[:160]


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


def build_query(
    branch: str = "",
    pull_request: str = "",
    **params: str,
) -> dict[str, str]:
    query = {key: value for key, value in params.items() if value}
    if pull_request:
        query["pullRequest"] = pull_request
    elif branch:
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


def failed_quality_gate_conditions(
    conditions: Sequence[QualityGateCondition],
) -> list[QualityGateCondition]:
    return [
        condition
        for condition in conditions
        if isinstance(condition, dict)
        and str(condition.get("status", "")).upper() in {"ERROR", "WARN"}
    ]


def condition_metric(condition: QualityGateCondition) -> str:
    return str(condition.get("metricKey", "")).strip() or "unknown"


def new_ci_suppression_entries(
    suppression_file: Path,
    pull_request_base_sha: str,
) -> list[str]:
    if len(pull_request_base_sha) != 40 or any(
        character not in "0123456789abcdef" for character in pull_request_base_sha
    ):
        raise RuntimeError("Pull-request base SHA must be a full lowercase Git commit.")
    try:
        diff = subprocess.run(
            [
                "git",
                "-C",
                str(suppression_file.parent),
                "diff",
                "--unified=0",
                pull_request_base_sha,
                "--",
                CI_SUPPRESSION_FILE,
            ],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError, UnicodeDecodeError) as exc:
        raise RuntimeError("Failed to compare CI suppressions with the PR base.") from exc

    return [
        line[1:]
        for line in diff.stdout.splitlines()
        if line.startswith("+") and not line.startswith("+++")
    ]


def load_ci_suppressions(
    suppression_file: Path,
    pull_request_base_sha: str,
) -> dict[str, tuple[str, str]]:
    if not suppression_file.exists() or not pull_request_base_sha:
        return {}

    applied: dict[str, tuple[str, str]] = {}
    for entry in new_ci_suppression_entries(
        suppression_file,
        pull_request_base_sha,
    ):
        if not entry or entry.startswith("#"):
            continue
        suppression_type, separator, reason = entry.partition(":")
        suppression_type = suppression_type.strip()
        reason = reason.strip()
        conditions = CI_SUPPRESSION_CONDITIONS.get(suppression_type)
        if not separator or conditions is None:
            raise RuntimeError("New CI suppression has unsupported format or type.")
        if not reason:
            raise RuntimeError("New CI suppression requires a reason.")

        for condition in conditions:
            applied[condition] = (suppression_type, reason)

    return applied


def quality_gate_status_after_suppressions(
    gate_status: str,
    failed_conditions: Sequence[QualityGateCondition],
    suppressions: dict[str, tuple[str, str]],
) -> str:
    if gate_status == "OK" or not failed_conditions:
        return gate_status
    if all(condition_metric(condition) in suppressions for condition in failed_conditions):
        return "OK"
    return gate_status


def print_failed_conditions(conditions: Sequence[QualityGateCondition]) -> None:
    if not conditions:
        return

    log_info("Failed conditions:")
    for condition in conditions:
        metric = condition_metric(condition)
        actual = str(condition.get("actualValue", "n/a")).strip() or "n/a"
        comparator = str(condition.get("comparator", "")).strip() or "?"
        threshold = str(condition.get("errorThreshold", "n/a")).strip() or "n/a"
        log_info(f"- {metric}: actual {actual} {comparator} threshold {threshold}")
    log_info()


def print_applied_suppressions(
    failed_conditions: Sequence[QualityGateCondition],
    suppressions: dict[str, tuple[str, str]],
) -> None:
    applied_metrics = {
        condition_metric(condition)
        for condition in failed_conditions
        if condition_metric(condition) in suppressions
    }
    if not applied_metrics:
        return

    log_info("Applied CI suppressions:")
    for metric in sorted(applied_metrics):
        suppression_type, reason = suppressions[metric]
        log_info(f"- {suppression_type} ({metric}): {reason}")
    log_info()


def iter_secondary_location_entries(
    issue: IssuePayload,
    limit: int,
) -> Sequence[IssueLocationPayload]:
    if limit <= 0:
        return []

    flows = issue.get("flows")
    if not isinstance(flows, list):
        return []

    locations: list[IssueLocationPayload] = []
    for flow in flows:
        if not isinstance(flow, dict):
            continue
        flow_locations = flow.get("locations")
        if not isinstance(flow_locations, list):
            continue
        locations.extend(
            cast(IssueLocationPayload, location)
            for location in flow_locations
            if isinstance(location, dict)
        )
        if len(locations) >= limit:
            return locations[:limit]
    return locations


def format_secondary_location(
    project_key: str,
    location: IssueLocationPayload,
) -> str:
    component = str(location.get("component", "")).strip()
    message = str(location.get("msg", "")).strip()
    line_number = extract_line_number(location)
    display_location = format_location(project_key, component, line_number)
    if message and message != "+1":
        return f"{display_location} {message}"
    return display_location


def collect_secondary_locations(
    issue: IssuePayload,
    project_key: str,
    limit: int = 3,
) -> list[str]:
    secondary_locations: list[str] = []
    for location in iter_secondary_location_entries(issue, limit):
        secondary_locations.append(format_secondary_location(project_key, location))
        if len(secondary_locations) >= limit:
            break

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


def log_context_scope(ctx: ReportTaskContext) -> None:
    if ctx.get("pullRequest"):
        log_info(f"Pull request: #{ctx['pullRequest']}")
    if ctx.get("pullRequestBranch"):
        log_info(f"PR branch: {ctx['pullRequestBranch']}")
    if ctx.get("pullRequestBase"):
        log_info(f"PR base: {ctx['pullRequestBase']}")
    if ctx.get("branch"):
        log_info(f"Branch: {ctx['branch']}")


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
                branch=ctx.get("branch", ""),
                pull_request=ctx.get("pullRequest", ""),
                componentKeys=ctx["projectKey"],
                resolved="false",
                inNewCodePeriod="true",
                ps=str(max(1, issue_limit)),
                s="FILE_LINE",
                asc="true",
            ),
        ),
    )


def print_report_header(ctx: ReportTaskContext) -> None:
    log_info("Sonar quality gate report")
    log_info(f"Project: {ctx['projectKey']}")
    log_context_scope(ctx)
    if ctx.get("dashboardUrl"):
        log_info(f"Dashboard: {ctx['dashboardUrl']}")
    log_info(f"CE task id: {ctx['ceTaskId']}")
    log_info()


def fetch_quality_gate_project_status(
    ctx: ReportTaskContext,
    analysis_id: str,
    token: str,
) -> ProjectStatusPayload:
    quality_gate = cast(
        QualityGatePayload,
        api_get_json(
            ctx["serverUrl"],
            "/api/qualitygates/project_status",
            token,
            {"analysisId": analysis_id},
        ),
    )
    project_status = quality_gate.get("projectStatus")
    if not isinstance(project_status, dict):
        raise RuntimeError("Sonar report error: quality gate response is missing projectStatus.")
    return cast(ProjectStatusPayload, project_status)


def handle_issue_snapshot_error(gate_status: str, exc: RuntimeError) -> int:
    if gate_status == "OK":
        log_warning(f"Issue snapshot unavailable: {exc}")
        log_info()
        log_info("Sonar quality gate passed.")
        return int(ExitCode.OK)
    log_error(f"Issue snapshot unavailable: {exc}")
    log_info()
    return int(ExitCode.SCRIPT_ERROR)


def report_issue_and_hotspot_snapshots(
    ctx: ReportTaskContext,
    args: argparse.Namespace,
    token: str,
    gate_status: str,
    workspace_root: Path,
    rule_cache: dict[str, str],
) -> int:
    try:
        issues = load_issue_snapshot(ctx, token, args.issue_limit)
    except RuntimeError as exc:
        return handle_issue_snapshot_error(gate_status, exc)

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

    if gate_status == "OK":
        log_info("Sonar quality gate passed.")
        return int(ExitCode.OK)

    try:
        hotspots = cast(
            HotspotsSearchPayload,
            api_get_json(
                ctx["serverUrl"],
                "/api/hotspots/search",
                token,
                build_query(
                    branch=ctx.get("branch", ""),
                    pull_request=ctx.get("pullRequest", ""),
                    projectKey=ctx["projectKey"],
                    status="TO_REVIEW",
                    inNewCodePeriod="true",
                    ps=str(max(1, args.hotspot_limit)),
                ),
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
        log_warning(f"Security hotspot snapshot unavailable: {exc}")
        log_info()

    return int(ExitCode.QUALITY_GATE_FAILED)


def main(argv: Sequence[str] | None = None) -> int:
    configure_logging()
    args = parse_args(argv)
    token = os.environ.get("SONAR_TOKEN", "").strip()
    if not token:
        return fail("Required environment variable is missing: SONAR_TOKEN")

    try:
        ctx = load_report_task(
            args.report_task_file,
            args.branch,
            args.pull_request_key,
            args.pull_request_branch,
            args.pull_request_base,
        )
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

    print_report_header(ctx)

    task_status = str(task.get("status", "UNKNOWN")).upper()
    log_info(f"Compute-engine status: {task_status}")
    if task_status != "SUCCESS":
        error_message = str(task.get("errorMessage", "")).strip() or "No error message provided."
        log_error(f"Compute-engine failure message: {error_message}")
        return int(ExitCode.EXECUTION_ERROR)

    analysis_id = str(task.get("analysisId", "")).strip()
    if not analysis_id:
        return fail("Sonar report error: compute-engine task completed without an analysisId.")

    try:
        project_status = fetch_quality_gate_project_status(ctx, analysis_id, token)
    except RuntimeError as exc:
        return fail(f"Sonar report error: {exc}")

    gate_status = str(project_status.get("status", "UNKNOWN")).upper()
    log_info(f"Quality gate status: {gate_status}")
    log_info()

    conditions = project_status.get("conditions")
    failed_conditions: list[QualityGateCondition] = []
    if isinstance(conditions, list):
        failed_conditions = failed_quality_gate_conditions(
            cast(list[QualityGateCondition], conditions)
        )
        print_failed_conditions(failed_conditions)

    suppression_file = workspace_root / CI_SUPPRESSION_FILE
    try:
        suppressions = load_ci_suppressions(
            suppression_file,
            args.pull_request_base_sha,
        )
    except RuntimeError as exc:
        return fail(f"Sonar report error: {exc}")
    print_applied_suppressions(failed_conditions, suppressions)
    effective_gate_status = quality_gate_status_after_suppressions(
        gate_status,
        failed_conditions,
        suppressions,
    )

    return report_issue_and_hotspot_snapshots(
        ctx,
        args,
        token,
        effective_gate_status,
        workspace_root,
        rule_cache,
    )


if __name__ == "__main__":
    configure_logging()
    raise SystemExit(main())
