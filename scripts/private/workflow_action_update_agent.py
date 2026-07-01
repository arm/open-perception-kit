#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from datetime import datetime, timedelta, timezone
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from agent_workflows.contracts import AgentInstance  # noqa: E402
from agent_workflows.model_config import resolve_agent_model  # noqa: E402


TICKET_RE = re.compile(r"^[A-Z][A-Z0-9]*-[0-9]+$")
GITHUB_WORKSPACE = os.environ.get("GITHUB_WORKSPACE", "").strip()
HELPER_ROOT = Path(__file__).resolve().parents[2]
REPO_ROOT = Path(GITHUB_WORKSPACE).resolve() if GITHUB_WORKSPACE else HELPER_ROOT
DEFAULT_PROFILE_PATH = HELPER_ROOT / ".github/agent-workflows/workflow-repair/profiles/profile.json"
PR_TEMPLATE_PATH = REPO_ROOT / ".github/PULL_REQUEST_TEMPLATE.md"
MARKDOWN_TEMPLATE_ROOT = HELPER_ROOT / ".github/agent-workflows/workflow-repair/prompts"
PR_AUTOMATION_START = "<!-- workflow-action-update-agent:automation:start -->"
PR_AUTOMATION_END = "<!-- workflow-action-update-agent:automation:end -->"
PR_DESCRIPTION_START = "<!-- workflow-action-update-agent:description:start -->"
PR_DESCRIPTION_END = "<!-- workflow-action-update-agent:description:end -->"
DISPLAY_NAME_TOKEN = "{{DISPLAY_NAME}}"
PROMPT_CONTEXT_FILES_TOKEN = "{{PROMPT_CONTEXT_FILES}}"
VALIDATION_COMMANDS_TOKEN = "{{VALIDATION_COMMANDS}}"
CONTEXT_ROOT_TOKEN = "{{CONTEXT_ROOT}}"
PR_NUMBER_TOKEN = "{{PR_NUMBER}}"
REPAIR_BRANCH_TOKEN = "{{REPAIR_BRANCH}}"
REVIEW_RECOMMENDATION_TOKEN = "{{REVIEW_RECOMMENDATION}}"
REVIEW_RUN_ID_TOKEN = "{{REVIEW_RUN_ID}}"
REVIEW_STATE_JSON_TOKEN = "{{REVIEW_STATE_JSON}}"
REVIEW_SUMMARY_TOKEN = "{{REVIEW_SUMMARY}}"
REVIEW_WORKFLOW_NAME_TOKEN = "{{REVIEW_WORKFLOW_NAME}}"
SOURCE_RUN_ID_TOKEN = "{{SOURCE_RUN_ID}}"
SOURCE_RUN_URL_TOKEN = "{{SOURCE_RUN_URL}}"
SOURCE_WORKFLOW_NAME_TOKEN = "{{SOURCE_WORKFLOW_NAME}}"
TARGET_BRANCH_TOKEN = "{{TARGET_BRANCH}}"
TICKET_ID_TOKEN = "{{TICKET_ID}}"
WAIT_TIMEOUT_SECONDS = 1800
STABILIZATION_MAX_ATTEMPTS = 5
STABILIZER_WORKFLOW_FILE = "agent-stabilize-pr.yml"
STABILIZER_WORKFLOW_NAME = "Agent Stabilize PR"
PULL_REQUEST_RUN_GRACE_SECONDS = 60


def run_command(
    args: list[str],
    *,
    capture_output: bool = False,
    check: bool = True,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        check=check,
        text=True,
        capture_output=capture_output,
        env=env,
    )


def run_shell_command(command: str, *, env: dict[str, str] | None = None) -> None:
    subprocess.run(
        command,
        check=True,
        text=True,
        shell=True,
        executable="/bin/bash",
        env=env,
    )


def parse_json_command(args: list[str]) -> object:
    completed = run_command(args, capture_output=True)
    return json.loads(completed.stdout)


def github_api_base_url() -> str:
    return os.environ.get("GITHUB_API_URL", "https://api.github.com").rstrip("/")


def github_api_token() -> str:
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN", "")
    if not token:
        raise RuntimeError("GITHUB_TOKEN or GH_TOKEN is required for GitHub API access.")
    return token


def github_api_request(url: str) -> bytes:
    request = urllib.request.Request(
        url,
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {github_api_token()}",
            "User-Agent": "workflow-action-update-agent",
            "X-GitHub-Api-Version": "2022-11-28",
        },
    )
    with urllib.request.urlopen(request) as response:
        return response.read()


class _NoRedirectHandler(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):  # noqa: ANN001
        return None


def download_github_archive(url: str) -> bytes:
    request = urllib.request.Request(
        url,
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {github_api_token()}",
            "User-Agent": "workflow-action-update-agent",
            "X-GitHub-Api-Version": "2022-11-28",
        },
    )
    opener = urllib.request.build_opener(_NoRedirectHandler)
    try:
        with opener.open(request) as response:
            return response.read()
    except urllib.error.HTTPError as exc:
        if exc.code not in {301, 302, 303, 307, 308}:
            raise
        location = exc.headers.get("Location", "")
        if not location:
            raise
        with urllib.request.urlopen(location) as response:
            return response.read()


def extract_archive_bytes(archive_bytes: bytes, destination: Path) -> list[Path]:
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(archive_bytes)) as archive:
        archive.extractall(destination)
    return sorted(path for path in destination.rglob("*") if path.is_file())


def github_api_json(endpoint_or_url: str) -> object:
    if endpoint_or_url.startswith("http://") or endpoint_or_url.startswith("https://"):
        url = endpoint_or_url
    else:
        url = f"{github_api_base_url()}/{endpoint_or_url.lstrip('/')}"
    return json.loads(github_api_request(url))


def write_outputs(values: dict[str, str], output_path: str | None = None) -> None:
    target = output_path or os.environ.get("GITHUB_OUTPUT")
    if not target:
        raise ValueError("GITHUB_OUTPUT is not set and no explicit output path was provided.")
    with Path(target).open("a", encoding="utf-8") as output_file:
        for key, value in values.items():
            output_file.write(f"{key}={value}\n")


def resolve_repo_path(path_value: str) -> Path:
    path = Path(path_value)
    if path.is_absolute():
        return path
    return (REPO_ROOT / path).resolve()


def default_profile_path_argument() -> str:
    try:
        return str(DEFAULT_PROFILE_PATH.relative_to(REPO_ROOT))
    except ValueError:
        return str(DEFAULT_PROFILE_PATH)


def profile_config_root(profile_path: str) -> Path:
    path = resolve_repo_path(profile_path or default_profile_path_argument())
    try:
        relative_parts = path.relative_to(REPO_ROOT).parts
    except ValueError:
        return path.parent

    marker_parts = Path(".github/agent-workflows/workflow-repair/profiles").parts
    for index in range(0, len(relative_parts) - len(marker_parts) + 1):
        if relative_parts[index:index + len(marker_parts)] == marker_parts:
            return REPO_ROOT.joinpath(*relative_parts[:index]).resolve()
    return REPO_ROOT


def load_json_file(path: Path) -> object:
    return json.loads(path.read_text(encoding="utf-8"))


def load_profile(profile_path: str = "") -> dict[str, object]:
    path = resolve_repo_path(profile_path or default_profile_path_argument())
    if not path.is_file():
        raise ValueError(f"Workflow action update agent profile is missing: {path}")

    profile = load_json_file(path)
    if not isinstance(profile, dict):
        raise ValueError(f"Workflow action update agent profile must be a JSON object: {path}")

    required_string_keys = (
        "display_name",
        "automation_name",
        "repair_branch_template",
        "repair_branch_guard_regex",
        "pr_trigger_label",
        "pr_title_template",
        "commit_subject_template",
        "commit_notes_template",
        "pr_description_template",
        "agent_model_config",
    )
    required_list_keys = (
        "prompt_context_files",
        "validation_commands",
        "validation_workflows",
    )

    for key in required_string_keys:
        profile_string(profile, key)
    for key in required_list_keys:
        profile_list(profile, key)

    profile_validation_workflows(profile)
    return profile


def profile_bool(profile: dict[str, object], key: str, default: bool) -> bool:
    value = profile.get(key, default)
    if not isinstance(value, bool):
        raise ValueError(f"Profile key '{key}' must be a boolean.")
    return value


def profile_string(profile: dict[str, object], key: str) -> str:
    value = profile.get(key)
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"Profile key '{key}' must be a non-empty string.")
    return value


def profile_agent_model(profile: dict[str, object], agent_instance: AgentInstance, profile_path: str = "") -> str:
    model_config_path = Path(profile_string(profile, "agent_model_config"))
    if not model_config_path.is_absolute():
        model_config_path = profile_config_root(profile_path) / model_config_path
    return resolve_agent_model(
        model_config_path,
        agent_instance,
    )


def profile_optional_string(profile: dict[str, object], key: str, default: str = "") -> str:
    value = profile.get(key, default)
    if value in (None, ""):
        return default
    if not isinstance(value, str):
        raise ValueError(f"Profile key '{key}' must be a string when present.")
    return value.strip()


def profile_list(profile: dict[str, object], key: str) -> list[object]:
    value = profile.get(key)
    if not isinstance(value, list):
        raise ValueError(f"Profile key '{key}' must be a JSON array.")
    return value


def profile_string_list(profile: dict[str, object], key: str) -> list[str]:
    items = profile_list(profile, key)
    if not all(isinstance(item, str) and item.strip() for item in items):
        raise ValueError(f"Profile key '{key}' must contain only non-empty strings.")
    return [str(item) for item in items]


def profile_validation_workflows(profile: dict[str, object]) -> list[dict[str, object]]:
    workflows = profile_list(profile, "validation_workflows")
    parsed: list[dict[str, object]] = []
    for item in workflows:
        if not isinstance(item, dict):
            raise ValueError("Profile key 'validation_workflows' must contain only JSON objects.")
        workflow_file = item.get("workflow_file")
        workflow_name = item.get("workflow_name")
        review_state_script = item.get("review_state_script", "")
        allowed_review_recommendations = item.get("allowed_review_recommendations", [])
        workflow_dispatch_inputs = item.get("workflow_dispatch_inputs", {})
        if not isinstance(workflow_file, str) or not workflow_file.strip():
            raise ValueError("Each validation workflow must define a non-empty 'workflow_file'.")
        if not isinstance(workflow_name, str) or not workflow_name.strip():
            raise ValueError("Each validation workflow must define a non-empty 'workflow_name'.")
        if review_state_script and (not isinstance(review_state_script, str) or not review_state_script.strip()):
            raise ValueError(
                "Each validation workflow 'review_state_script' value must be a non-empty string when present.")
        if not isinstance(allowed_review_recommendations, list):
            raise ValueError(
                "Each validation workflow 'allowed_review_recommendations' value must be a JSON array when present.")
        if not isinstance(workflow_dispatch_inputs, dict):
            raise ValueError(
                "Each validation workflow 'workflow_dispatch_inputs' value must be a JSON object when present."
            )
        if review_state_script:
            if not allowed_review_recommendations:
                raise ValueError(
                    "Each validation workflow with a 'review_state_script' must define 'allowed_review_recommendations'."
                )
            if not all(
                isinstance(recommendation, str) and recommendation.strip()
                for recommendation in allowed_review_recommendations
            ):
                raise ValueError(
                    "Each validation workflow 'allowed_review_recommendations' value must contain only non-empty strings."
                )
        elif allowed_review_recommendations:
            raise ValueError(
                "Each validation workflow with 'allowed_review_recommendations' must also define 'review_state_script'."
            )
        normalized_dispatch_inputs: dict[str, str] = {}
        for input_name, input_value in workflow_dispatch_inputs.items():
            if not isinstance(input_name, str) or not input_name.strip():
                raise ValueError(
                    "Each validation workflow 'workflow_dispatch_inputs' key must be a non-empty string."
                )
            if not isinstance(input_value, str) or not input_value.strip():
                raise ValueError(
                    "Each validation workflow 'workflow_dispatch_inputs' value must be a non-empty string."
                )
            normalized_dispatch_inputs[input_name.strip()] = input_value.strip()
        parsed.append(
            {
                "workflow_file": workflow_file,
                "workflow_name": workflow_name,
                "review_state_script": str(review_state_script or ""),
                "allowed_review_recommendations": [
                    str(recommendation).strip().lower()
                    for recommendation in allowed_review_recommendations
                ],
                "workflow_dispatch_inputs": normalized_dispatch_inputs,
            }
        )
    return parsed


def workflow_allowed_review_recommendations(workflow: dict[str, object]) -> list[str]:
    recommendations = workflow.get("allowed_review_recommendations", [])
    if not isinstance(recommendations, list):
        raise ValueError(
            "Validation workflow 'allowed_review_recommendations' value must be a JSON array."
        )
    return [
        str(recommendation).strip().lower()
        for recommendation in recommendations
    ]


def format_profile_template(template: str, values: dict[str, str]) -> str:
    try:
        return template.format_map(values)
    except KeyError as exc:
        missing_key = exc.args[0]
        raise ValueError(f"Profile template is missing a value for '{missing_key}'.") from exc


def render_bullet_list(items: list[str]) -> str:
    return "\n".join(f"- `{item}`" for item in items)


def render_repair_ci_badge(repair_branch: str) -> str:
    return (
        "[![Perception Experience Kit CI Pipeline]"
        "(https://github.com/Arm-Debug/amp-dev-forge/actions/workflows/pek-ci.yml/badge.svg"
        f"?branch={repair_branch})"
        "(https://github.com/Arm-Debug/amp-dev-forge/actions/workflows/pek-ci.yml)"
    )


def load_markdown_template(name: str) -> str:
    template_path = MARKDOWN_TEMPLATE_ROOT / name
    if not template_path.is_file():
        raise ValueError(f"Markdown template file is missing: {template_path}")
    return template_path.read_text(encoding="utf-8")


def render_markdown_template(name: str, replacements: dict[str, str]) -> str:
    template = load_markdown_template(name)
    for token, replacement in replacements.items():
        template = template.replace(token, replacement)
    return template


def replace_marked_section(document: str, start_marker: str, end_marker: str, content: str) -> str:
    if start_marker not in document or end_marker not in document:
        raise ValueError(f"Template is missing marker section '{start_marker}' -> '{end_marker}'.")

    prefix, _, remainder = document.partition(start_marker)
    _, separator, suffix = remainder.partition(end_marker)
    if not separator:
        raise ValueError(f"Template is missing end marker '{end_marker}'.")

    return f"{prefix}{start_marker}\n{content.rstrip()}\n{end_marker}{suffix}"


def build_profile_context(
    profile: dict[str, object],
    *,
    source_run_id: str,
    source_run_url: str,
    source_workflow_name: str,
    repair_branch: str,
    target_branch: str,
    ticket_id: str,
) -> dict[str, str]:
    return {
        "automation_name": profile_string(profile, "automation_name"),
        "source_run_id": source_run_id,
        "source_run_url": source_run_url,
        "source_workflow_name": source_workflow_name,
        "repair_branch": repair_branch,
        "target_branch": target_branch,
        "ticket_id": ticket_id,
        "pr_trigger_label": profile_string(profile, "pr_trigger_label"),
    }


def render_pr_body_from_template(
    *,
    template_text: str,
    description: str,
    repair_branch: str,
    automation_name: str,
) -> str:
    automation_notice = textwrap.dedent(
        f"""
        Automation actor: `{automation_name}` bot run using `EXPKITS_AGENT_TOKEN`.
        GitHub may display the PAT owner as the PR author; this marker shows the PR was opened by automation.

        {render_repair_ci_badge(repair_branch)}
        """
    ).strip()

    rendered = replace_marked_section(template_text, PR_AUTOMATION_START, PR_AUTOMATION_END, automation_notice)
    rendered = replace_marked_section(
        rendered,
        PR_DESCRIPTION_START,
        PR_DESCRIPTION_END,
        textwrap.dedent(description).strip(),
    )
    return rendered.rstrip() + "\n"


def render_pr_body(*, profile: dict[str, object], description: str, repair_branch: str) -> str:
    if not PR_TEMPLATE_PATH.exists():
        raise ValueError(f"PR template file is missing: {PR_TEMPLATE_PATH}")

    return render_pr_body_from_template(
        template_text=PR_TEMPLATE_PATH.read_text(encoding="utf-8"),
        description=description,
        repair_branch=repair_branch,
        automation_name=profile_string(profile, "automation_name"),
    )


def render_repair_metadata_values(
    *,
    profile: dict[str, object],
    source_run_id: str,
    source_run_url: str,
    source_workflow_name: str,
    repair_branch: str,
    target_branch: str = "",
    ticket_id: str = "",
) -> tuple[str, str, str, str]:
    context = build_profile_context(
        profile,
        source_run_id=source_run_id,
        source_run_url=source_run_url,
        source_workflow_name=source_workflow_name,
        repair_branch=repair_branch,
        target_branch=target_branch,
        ticket_id=ticket_id,
    )
    description = format_profile_template(profile_string(profile, "pr_description_template"), context)
    pr_title = format_profile_template(profile_string(profile, "pr_title_template"), context)
    commit_subject = format_profile_template(profile_string(profile, "commit_subject_template"), context)
    commit_notes = format_profile_template(profile_string(profile, "commit_notes_template"), context)

    return (
        render_pr_body(profile=profile, description=description, repair_branch=repair_branch),
        pr_title + "\n",
        commit_subject + "\n",
        commit_notes,
    )


def write_repair_metadata_files(
    *,
    profile: dict[str, object],
    source_run_id: str,
    source_run_url: str,
    source_workflow_name: str,
    repair_branch: str,
    target_branch: str,
    ticket_id: str,
    body_file: str,
    pr_title_file: str,
    commit_subject_file: str,
    commit_notes_file: str,
) -> None:
    body, pr_title, commit_subject, commit_notes = render_repair_metadata_values(
        profile=profile,
        source_run_id=source_run_id,
        source_run_url=source_run_url,
        source_workflow_name=source_workflow_name,
        repair_branch=repair_branch,
        target_branch=target_branch,
        ticket_id=ticket_id,
    )
    Path(body_file).write_text(body, encoding="utf-8")
    Path(pr_title_file).write_text(pr_title, encoding="utf-8")
    Path(commit_subject_file).write_text(commit_subject, encoding="utf-8")
    Path(commit_notes_file).write_text(commit_notes, encoding="utf-8")


def parse_timestamp(value: str) -> datetime:
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def read_json_file(path: Path) -> dict[str, object]:
    payload = load_json_file(path)
    if not isinstance(payload, dict):
        raise ValueError(f"Expected JSON object in {path}.")
    return payload


def read_pr_details(pr_number: str) -> dict[str, str]:
    repository = os.environ.get("GITHUB_REPOSITORY", "")
    if not repository:
        raise RuntimeError("GITHUB_REPOSITORY is required to resolve PR details.")
    payload = github_api_json(f"repos/{repository}/pulls/{pr_number}")
    if not isinstance(payload, dict):
        raise RuntimeError(f"Unexpected PR payload for PR #{pr_number}.")
    head = dict(payload.get("head") or {})
    base = dict(payload.get("base") or {})
    return {
        "repair_branch": str(head.get("ref") or ""),
        "head_sha": str(head.get("sha") or ""),
        "target_branch": str(base.get("ref") or ""),
    }


def build_validation_dispatch_context(
    *,
    pr_number: str,
    repair_branch: str,
    head_sha: str,
    target_branch: str,
    source_run_id: str,
    ticket_id: str,
) -> dict[str, str]:
    return {
        "pr_number": pr_number,
        "repair_branch": repair_branch,
        "head_ref": repair_branch,
        "head_sha": head_sha,
        "target_branch": target_branch,
        "source_run_id": source_run_id,
        "ticket_id": ticket_id,
    }


def render_validation_workflow_dispatch_inputs(
    *,
    workflow: dict[str, object],
    dispatch_context: dict[str, str],
) -> dict[str, str]:
    raw_inputs = workflow.get("workflow_dispatch_inputs", {})
    if not isinstance(raw_inputs, dict):
        return {}
    return {
        str(name): format_profile_template(str(template), dispatch_context)
        for name, template in raw_inputs.items()
    }


def find_latest_workflow_run_candidate(
    *,
    repository: str,
    workflow_file: str,
    repair_branch: str,
    head_sha: str,
    events: list[str] | None = None,
    created_after: datetime | None = None,
) -> dict[str, str]:
    payload = github_api_json(
        f"repos/{repository}/actions/workflows/{workflow_file}/runs?branch={repair_branch}&per_page=20",
    )
    workflow_runs = payload.get("workflow_runs", []) if isinstance(payload, dict) else []
    candidates: list[tuple[datetime, dict[str, str]]] = []
    allowed_events = set(events or [])
    for run in workflow_runs:
        if not isinstance(run, dict):
            continue
        created_at = str(run.get("created_at") or "")
        event = str(run.get("event") or "")
        if not created_at or str(run.get("head_sha") or "") != head_sha:
            continue
        if allowed_events and event not in allowed_events:
            continue
        if created_after is not None and parse_timestamp(created_at) < created_after:
            continue
        candidates.append(
            (
                parse_timestamp(created_at),
                {
                    "id": str(run.get("id") or ""),
                    "event": event,
                    "status": str(run.get("status") or ""),
                    "conclusion": str(run.get("conclusion") or ""),
                    "created_at": created_at,
                },
            )
        )
    candidates.sort(reverse=True)
    return candidates[0][1] if candidates else {}


def find_latest_workflow_run_for_head(
    *,
    repository: str,
    workflow_file: str,
    repair_branch: str,
    head_sha: str,
) -> str:
    candidate = find_latest_workflow_run_candidate(
        repository=repository,
        workflow_file=workflow_file,
        repair_branch=repair_branch,
        head_sha=head_sha,
    )
    return str(candidate.get("id") or "")


def read_review_artifact_state(*, repository: str, run_id: str, head_sha: str) -> dict[str, object]:
    if not run_id:
        return dict()

    with tempfile.TemporaryDirectory(prefix="workflow-action-update-agent-review-artifact-") as temp_dir:
        payload = github_api_json(f"repos/{repository}/actions/runs/{run_id}/artifacts?per_page=100")
        artifacts = payload.get("artifacts", []) if isinstance(payload, dict) else []
        archive_url = ""
        for artifact in artifacts:
            if not isinstance(artifact, dict):
                continue
            if str(artifact.get("name") or "") != "agent-review-out":
                continue
            if bool(artifact.get("expired")):
                continue
            archive_url = str(artifact.get("archive_download_url") or "")
            if archive_url:
                break
        if not archive_url:
            return dict()

        zip_path = Path(temp_dir) / "agent-review-out.zip"
        zip_path.write_bytes(download_github_archive(archive_url))
        artifact_root = Path(temp_dir) / "artifact"
        artifact_root.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(zip_path) as archive:
            archive.extractall(artifact_root)
        review_json = next(iter(sorted(artifact_root.rglob("review.json"))), None)
        if review_json is None:
            return dict()
        review_state = read_json_file(review_json)
        review_state["run_id"] = run_id
        review_state["head_sha"] = head_sha
        return review_state


def wait_for_dispatched_workflow_run(
    *,
    repository: str,
    workflow_file: str,
    dispatch_nonce: str,
) -> str:
    deadline = time.time() + WAIT_TIMEOUT_SECONDS

    while time.time() < deadline:
        payload = parse_json_command(
            [
                "gh",
                "api",
                f"repos/{repository}/actions/workflows/{workflow_file}/runs?event=workflow_dispatch&per_page=20",
            ],
        )
        workflow_runs = payload.get("workflow_runs", []) if isinstance(payload, dict) else []
        candidates: list[tuple[datetime, str]] = []
        for run in workflow_runs:
            if not isinstance(run, dict):
                continue
            display_title = str(run.get("display_title") or "")
            created_at = str(run.get("created_at") or "")
            if dispatch_nonce not in display_title or not created_at:
                continue
            candidates.append((parse_timestamp(created_at), str(run.get("id") or "")))

        candidates.sort(reverse=True)
        if candidates:
            return candidates[0][1]

        time.sleep(5)

    raise RuntimeError(
        f"Timed out waiting for dispatched {workflow_file} run containing nonce '{dispatch_nonce}'.",
    )


def wait_for_existing_workflow_run(
    *,
    repository: str,
    workflow_file: str,
    repair_branch: str,
    head_sha: str,
    timeout_seconds: int,
) -> dict[str, str]:
    deadline = time.time() + timeout_seconds

    while time.time() < deadline:
        candidate = find_latest_workflow_run_candidate(
            repository=repository,
            workflow_file=workflow_file,
            repair_branch=repair_branch,
            head_sha=head_sha,
            events=["pull_request"],
        )
        if candidate:
            return candidate
        time.sleep(5)

    return {}


def dispatch_validation_workflow(
    *,
    repository: str,
    workflow_file: str,
    workflow_name: str,
    repair_branch: str,
    head_sha: str,
    workflow_inputs: dict[str, str],
) -> str:
    dispatched_after = datetime.now(timezone.utc) - timedelta(seconds=5)
    command = [
        "gh",
        "workflow",
        "run",
        workflow_file,
        "--ref",
        repair_branch,
    ]
    for key, value in workflow_inputs.items():
        command.extend(["-f", f"{key}={value}"])
    run_command(command, capture_output=True)

    deadline = time.time() + WAIT_TIMEOUT_SECONDS
    while time.time() < deadline:
        candidate = find_latest_workflow_run_candidate(
            repository=repository,
            workflow_file=workflow_file,
            repair_branch=repair_branch,
            head_sha=head_sha,
            events=["workflow_dispatch"],
            created_after=dispatched_after,
        )
        if candidate:
            run_id = str(candidate.get("id") or "")
            print(f"Watching {workflow_name} run {run_id} for {repair_branch}")
            wait_for_workflow_run_completion(
                repository=repository,
                workflow_name=workflow_name,
                run_id=run_id,
            )
            return run_id
        time.sleep(5)

    raise RuntimeError(f"Timed out waiting for dispatched {workflow_name} on {repair_branch}")


def ensure_validation_workflow_run(
    *,
    repository: str,
    workflow: dict[str, object],
    repair_branch: str,
    head_sha: str,
    dispatch_context: dict[str, str],
) -> tuple[str, str]:
    workflow_file = str(workflow.get("workflow_file") or "")
    workflow_name = str(workflow.get("workflow_name") or workflow_file)
    existing_run = wait_for_existing_workflow_run(
        repository=repository,
        workflow_file=workflow_file,
        repair_branch=repair_branch,
        head_sha=head_sha,
        timeout_seconds=PULL_REQUEST_RUN_GRACE_SECONDS,
    )
    if existing_run:
        run_id = str(existing_run.get("id") or "")
        print(f"Watching {workflow_name} run {run_id} for {repair_branch}")
        wait_for_workflow_run_completion(
            repository=repository,
            workflow_name=workflow_name,
            run_id=run_id,
        )
        return run_id, str(existing_run.get("event") or "pull_request")

    run_id = dispatch_validation_workflow(
        repository=repository,
        workflow_file=workflow_file,
        workflow_name=workflow_name,
        repair_branch=repair_branch,
        head_sha=head_sha,
        workflow_inputs=render_validation_workflow_dispatch_inputs(
            workflow=workflow,
            dispatch_context=dispatch_context,
        ),
    )
    return run_id, "workflow_dispatch"


def dispatch_stabilizer_workflow(
    *,
    repository: str,
    pr_number: str,
    head_sha: str,
    source_run_id: str,
    ticket_id: str,
    profile_path: str,
    context_root: str,
    dispatch_ref: str,
    dispatch_nonce: str,
) -> str:
    command = [
        "gh",
        "workflow",
        "run",
        STABILIZER_WORKFLOW_FILE,
        "--ref",
        dispatch_ref,
        "-f",
        f"pr_number={pr_number}",
        "-f",
        f"head_sha={head_sha}",
        "-f",
        f"source_run_id={source_run_id}",
        "-f",
        f"ticket_id={ticket_id}",
        "-f",
        f"profile_path={profile_path}",
        "-f",
        f"context_root={context_root}",
        "-f",
        f"dispatch_nonce={dispatch_nonce}",
    ]
    run_command(command, capture_output=True)
    run_id = wait_for_dispatched_workflow_run(
        repository=repository,
        workflow_file=STABILIZER_WORKFLOW_FILE,
        dispatch_nonce=dispatch_nonce,
    )
    print(f"Watching {STABILIZER_WORKFLOW_NAME} run {run_id} for PR #{pr_number}")
    wait_for_workflow_run_completion(
        repository=repository,
        workflow_name=STABILIZER_WORKFLOW_NAME,
        run_id=run_id,
    )
    return run_id


def build_markdown_documents(
    *,
    profile: dict[str, object],
    context_root: Path,
    source_run_id: str,
    source_run_url: str,
    source_workflow_name: str,
    target_branch: str,
    repair_branch: str,
    ticket_id: str,
    artifact_files: list[str],
) -> dict[str, str]:
    file_inventory = "# File Inventory\n\n" + "\n".join(f"- `{item}`" for item in artifact_files)
    prompt_context_files = render_bullet_list(profile_string_list(profile, "prompt_context_files"))
    validation_commands = render_bullet_list(profile_string_list(profile, "validation_commands"))

    return {
        "file-inventory.md": file_inventory,
        "failure-context.md": render_markdown_template(
            "failure-context.md.in",
            {
                CONTEXT_ROOT_TOKEN: context_root.as_posix(),
                REPAIR_BRANCH_TOKEN: repair_branch,
                SOURCE_RUN_ID_TOKEN: source_run_id,
                SOURCE_RUN_URL_TOKEN: source_run_url,
                SOURCE_WORKFLOW_NAME_TOKEN: source_workflow_name,
                TARGET_BRANCH_TOKEN: target_branch,
                TICKET_ID_TOKEN: ticket_id,
            },
        ),
        "ponytail-review.md": load_markdown_template("ponytail-review.md"),
        "constraints.md": load_markdown_template("constraints.md"),
        "validation.md": render_markdown_template(
            "validation.md.in",
            {VALIDATION_COMMANDS_TOKEN: validation_commands},
        ),
        "goal.md": render_markdown_template(
            "repair-goal.md.in",
            {
                DISPLAY_NAME_TOKEN: profile_string(profile, "display_name"),
                PROMPT_CONTEXT_FILES_TOKEN: prompt_context_files,
            },
        ),
    }


def command_resolve_inputs(args: argparse.Namespace) -> int:
    profile = load_profile(args.profile_path)
    ticket_id = args.ticket_id
    if not TICKET_RE.match(ticket_id):
        raise ValueError("ticket_id must match PROJECT-1234.")

    repository = os.environ["GITHUB_REPOSITORY"]
    current_ref_name = args.current_ref_name or "main"
    should_run = True
    skip_reason = ""
    source_run_id = args.source_run_id
    source_run_url = ""
    source_workflow_name = ""
    target_branch = args.target_branch or current_ref_name
    repair_branch = ""

    if not source_run_id:
        should_run = False
        skip_reason = "No source run ID was provided."
    else:
        run_json = parse_json_command(["gh", "api", f"repos/{repository}/actions/runs/{source_run_id}"])
        if not isinstance(run_json, dict):
            raise RuntimeError(f"Unexpected workflow run payload for source run {source_run_id}.")

        source_run_url = str(run_json.get("html_url") or "")
        source_workflow_name = str(run_json.get("name") or "")
        source_workflow_conclusion = str(run_json.get("conclusion") or "")
        source_head_branch = str(run_json.get("head_branch") or "")
        source_head_repository = str(dict(run_json.get("head_repository") or {}).get("full_name") or "")
        target_branch = args.target_branch or source_head_branch or current_ref_name

        repair_guard = re.compile(profile_string(profile, "repair_branch_guard_regex"))
        require_failure_conclusion = profile_bool(profile, "require_failure_conclusion", True)
        if require_failure_conclusion and source_workflow_conclusion != "failure":
            should_run = False
            skip_reason = f"Source workflow conclusion is '{source_workflow_conclusion}'."
        elif source_head_repository and source_head_repository != repository:
            should_run = False
            skip_reason = f"Source workflow head repository '{source_head_repository}' is not trusted."
        elif repair_guard.match(source_head_branch or ""):
            should_run = False
            skip_reason = "Source workflow already runs on a repair branch."
        else:
            repair_branch = format_profile_template(
                profile_string(profile, "repair_branch_template"),
                build_profile_context(
                    profile,
                    source_run_id=source_run_id,
                    source_run_url=source_run_url,
                    source_workflow_name=source_workflow_name,
                    repair_branch="",
                    target_branch=target_branch,
                    ticket_id=ticket_id,
                ),
            )

    write_outputs(
        {
            "should_run": "true" if should_run else "false",
            "skip_reason": skip_reason,
            "source_run_id": source_run_id or "",
            "source_run_url": source_run_url,
            "source_workflow_name": source_workflow_name,
            "target_branch": target_branch,
            "ticket_id": ticket_id,
            "repair_branch": repair_branch,
            "agent_model": profile_agent_model(profile, AgentInstance.REPAIR, args.profile_path),
        },
        args.github_output,
    )
    return 0


def command_collect_context(args: argparse.Namespace) -> int:
    context_root = Path(args.context_root)
    if context_root.exists():
        shutil.rmtree(context_root)
    artifact_root = context_root / "artifacts"
    artifact_root.mkdir(parents=True, exist_ok=True)

    repository = os.environ["GITHUB_REPOSITORY"]
    run_json = github_api_json(f"repos/{repository}/actions/runs/{args.source_run_id}")
    (context_root / "source-run.json").write_text(
        json.dumps(run_json, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    try:
        log_archive = download_github_archive(
            f"{github_api_base_url()}/repos/{repository}/actions/runs/{args.source_run_id}/logs",
        )
        with tempfile.TemporaryDirectory(prefix="workflow-action-update-agent-run-logs-") as temp_dir:
            log_root = Path(temp_dir) / "logs"
            log_files = extract_archive_bytes(log_archive, log_root)
            if log_files:
                with (context_root / "source-run.log").open("w", encoding="utf-8") as output_file:
                    for log_file in log_files:
                        relative_name = log_file.relative_to(log_root).as_posix()
                        log_text = log_file.read_text(encoding="utf-8", errors="replace")
                        output_file.write(f"===== {relative_name} =====\n")
                        output_file.write(log_text)
                        if not log_text.endswith("\n"):
                            output_file.write("\n")
                        output_file.write("\n")
            else:
                raise RuntimeError("Run log archive did not contain any files.")
    except (OSError, RuntimeError, urllib.error.HTTPError, urllib.error.URLError, zipfile.BadZipFile):
        (context_root / "source-run.log").write_text(
            f"Run logs were unavailable for {args.source_run_url}\n",
            encoding="utf-8",
        )

    try:
        payload = github_api_json(f"repos/{repository}/actions/runs/{args.source_run_id}/artifacts?per_page=100")
        artifacts = payload.get("artifacts", []) if isinstance(payload, dict) else []
        for artifact in artifacts:
            if not isinstance(artifact, dict):
                continue
            if bool(artifact.get("expired")):
                continue
            archive_url = str(artifact.get("archive_download_url") or "")
            artifact_name = str(artifact.get("name") or "").strip()
            artifact_id = str(artifact.get("id") or "").strip()
            if not archive_url or not artifact_name:
                continue
            destination = artifact_root / artifact_name
            if destination.exists():
                destination = artifact_root / f"{artifact_name}-{artifact_id or 'artifact'}"
            extract_archive_bytes(download_github_archive(archive_url), destination)
    except (OSError, RuntimeError, urllib.error.HTTPError, urllib.error.URLError, zipfile.BadZipFile):
        pass
    return 0


def command_build_markdown(args: argparse.Namespace) -> int:
    profile = load_profile(args.profile_path)
    context_root = Path(args.context_root)
    artifact_files = sorted(
        path.relative_to(context_root).as_posix()
        for path in context_root.rglob("*")
        if path.is_file()
    )

    documents = build_markdown_documents(
        profile=profile,
        context_root=context_root,
        source_run_id=args.source_run_id,
        source_run_url=args.source_run_url,
        source_workflow_name=args.source_workflow_name,
        target_branch=args.target_branch,
        repair_branch=args.repair_branch,
        ticket_id=args.ticket_id,
        artifact_files=artifact_files,
    )

    for name, content in documents.items():
        (context_root / name).write_text(
            textwrap.dedent(content).strip() + "\n",
            encoding="utf-8",
        )
    return 0


def command_package_patch(args: argparse.Namespace) -> int:
    run_command(["git", "add", "-A"])

    diff_check = run_command(["git", "diff", "--cached", "--quiet"], check=False)
    if diff_check.returncode == 0:
        write_outputs({"has_changes": "false"}, args.github_output)
        return 0

    patch_file = Path(args.patch_file)
    diffstat_file = Path(args.diffstat_file)
    patch_file.parent.mkdir(parents=True, exist_ok=True)
    diffstat_file.parent.mkdir(parents=True, exist_ok=True)

    patch_result = run_command(["git", "diff", "--cached", "--binary"], capture_output=True)
    diffstat_result = run_command(["git", "diff", "--cached", "--stat"], capture_output=True)
    patch_file.write_text(patch_result.stdout, encoding="utf-8")
    diffstat_file.write_text(diffstat_result.stdout, encoding="utf-8")
    write_outputs({"has_changes": "true"}, args.github_output)
    return 0


def command_require_generated_patch(args: argparse.Namespace) -> int:
    print(f"Agent did not produce repository changes for source run {args.source_run_id}.", file=sys.stderr)
    return 1


def command_apply_patch_and_push(args: argparse.Namespace) -> int:
    profile = load_profile(args.profile_path)
    patch_root = Path(args.patch_root)
    patch_file = next(iter(sorted(patch_root.rglob("workflow-action-update-agent.patch"))), None)
    if patch_file is None:
        discovered_files = "\n".join(
            path.as_posix()
            for path in sorted(patch_root.rglob("*"))
            if path.is_file()
        )
        if discovered_files:
            raise RuntimeError(f"Patch file was not found under {patch_root}.\n{discovered_files}")
        raise RuntimeError(f"Patch file was not found under {patch_root}.")

    write_repair_metadata_files(
        profile=profile,
        source_run_id=args.source_run_id,
        source_run_url=args.source_run_url,
        source_workflow_name=args.source_workflow_name,
        repair_branch=args.repair_branch,
        target_branch="",
        ticket_id=args.ticket_id,
        body_file=args.body_file,
        pr_title_file=args.pr_title_file,
        commit_subject_file=args.commit_subject_file,
        commit_notes_file=args.commit_notes_file,
    )

    run_command(["git", "config", "user.name", "github-actions[bot]"])
    run_command(["git", "config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com"])
    run_command(["git", "checkout", "-b", args.repair_branch])
    run_command(["git", "apply", "--index", str(patch_file)])

    commit_subject = Path(args.commit_subject_file).read_text(encoding="utf-8").strip()
    commit_notes = Path(args.commit_notes_file).read_text(encoding="utf-8").strip()
    commit_command = [
        "git",
        "commit",
        "-m",
        commit_subject,
        "-m",
        f"Task: {args.ticket_id}",
    ]
    if commit_notes:
        commit_command.extend(["-m", commit_notes])
    run_command(commit_command)
    run_command(["git", "push", "--set-upstream", "origin", args.repair_branch])

    head_sha = run_command(["git", "rev-parse", "HEAD"], capture_output=True).stdout.strip()
    write_outputs({"head_sha": head_sha}, args.github_output)
    return 0


def command_create_draft_pr(args: argparse.Namespace) -> int:
    profile = load_profile(args.profile_path)
    pr_title = Path(args.pr_title_file).read_text(encoding="utf-8").strip()
    pr_number = ""

    run_command(
        [
            "gh",
            "pr",
            "create",
            "--draft",
            "--base",
            args.target_branch,
            "--head",
            args.repair_branch,
            "--title",
            pr_title,
            "--body-file",
            args.body_file,
        ],
        capture_output=True,
    )
    pr_number = run_command(
        ["gh", "pr", "view", args.repair_branch, "--json", "number", "--jq", ".number"],
        capture_output=True,
    ).stdout.strip()

    label = profile_string(profile, "pr_trigger_label")
    if label:
        run_command(["gh", "pr", "edit", pr_number, "--add-label", label])

    write_outputs({"pr_number": pr_number}, args.github_output)
    return 0


def command_merge_pr(args: argparse.Namespace) -> int:
    merge_pr(args.pr_number)
    return 0


def merge_pr(pr_number: str) -> None:
    run_command(["gh", "pr", "ready", pr_number], check=False)
    run_command(["gh", "pr", "merge", pr_number, "--merge", "--delete-branch"])


def wait_for_workflow_run_completion(*, repository: str, workflow_name: str, run_id: str) -> None:
    deadline = time.time() + WAIT_TIMEOUT_SECONDS

    while time.time() < deadline:
        payload = parse_json_command(["gh", "api", f"repos/{repository}/actions/runs/{run_id}"])
        if not isinstance(payload, dict):
            raise RuntimeError(f"Unexpected workflow run payload for run {run_id}.")

        status = str(payload.get("status") or "")
        conclusion = str(payload.get("conclusion") or "")
        if status != "completed":
            time.sleep(15)
            continue
        if conclusion == "success":
            return
        if conclusion == "action_required":
            raise RuntimeError(
                f"{workflow_name} run {run_id} is waiting for manual approval (conclusion: action_required).\n"
                "Repository policy prevented unattended verification of the generated repair PR.",
            )
        raise RuntimeError(f"{workflow_name} run {run_id} concluded with '{conclusion}'.")

    raise RuntimeError(f"Timed out waiting for {workflow_name} run {run_id} to complete.")


def read_review_state(*, state_script: str, pr_number: str) -> dict[str, object]:
    script_path = resolve_repo_path(state_script)
    if not script_path.is_file():
        raise ValueError(f"Review state script is missing: {script_path}")

    env = dict(os.environ)
    if not env.get("GITHUB_TOKEN"):
        env["GITHUB_TOKEN"] = env.get("GH_TOKEN", "")
    env["GITHUB_PR_NUMBER"] = pr_number

    with tempfile.TemporaryDirectory(prefix="workflow-action-update-agent-review-") as temp_dir:
        output_path = Path(temp_dir) / "review-state.json"
        run_command(
            ["python3", str(script_path), "--output", str(output_path)],
            env=env,
        )
        return read_json_file(output_path)


def wait_for_review_state(
    *,
    pr_number: str,
    workflow_name: str,
    review_state_script: str,
    expected_run_id: str,
    head_sha: str,
) -> dict[str, object]:
    deadline = time.time() + WAIT_TIMEOUT_SECONDS

    while time.time() < deadline:
        review_state = read_review_state(
            state_script=review_state_script,
            pr_number=pr_number,
        )
        observed_run_id = str(review_state.get("run_id") or "")
        observed_head_sha = str(review_state.get("head_sha") or "")
        recommendation = str(review_state.get("overall_recommendation") or "").strip().lower()
        repository = os.environ.get("GITHUB_REPOSITORY", "")

        if observed_run_id == expected_run_id and observed_head_sha == head_sha and recommendation:
            print(
                f"Observed {workflow_name} recommendation {recommendation} from run {observed_run_id} for PR #{pr_number}"
            )
            return review_state

        if repository:
            artifact_state = read_review_artifact_state(
                repository=repository,
                run_id=expected_run_id,
                head_sha=head_sha,
            )
            artifact_recommendation = str(artifact_state.get("overall_recommendation") or "").strip().lower()
            if artifact_recommendation:
                print(
                    f"Observed {workflow_name} recommendation {artifact_recommendation} from artifact for run {expected_run_id} "
                    f"on PR #{pr_number}"
                )
                return artifact_state

        if observed_run_id != expected_run_id:
            time.sleep(15)
            continue
        if observed_head_sha != head_sha:
            time.sleep(15)
            continue

        time.sleep(15)

    raise RuntimeError(f"Timed out waiting for {workflow_name} recommendation on PR #{pr_number}")


def publish_review_state_to_pr(
    *,
    pr_number: str,
    head_sha: str,
    review_state: dict[str, object],
) -> None:
    script_path = resolve_repo_path(".github/agent-workflows/review/scripts/publish-review.py")
    if not script_path.is_file():
        raise ValueError(f"Agent review publish script is missing: {script_path}")

    env = dict(os.environ)
    if not env.get("GITHUB_TOKEN"):
        env["GITHUB_TOKEN"] = env.get("GH_TOKEN", "")
    if not env.get("GITHUB_TOKEN"):
        raise RuntimeError("GITHUB_TOKEN or GH_TOKEN is required to publish Agent review state.")
    env["GITHUB_PR_NUMBER"] = pr_number
    env["GITHUB_HEAD_SHA"] = head_sha
    env["GITHUB_RUN_ID"] = str(review_state.get("run_id") or "")

    with tempfile.TemporaryDirectory(prefix="workflow-action-update-agent-publish-review-") as temp_dir:
        input_path = Path(temp_dir) / "review.json"
        markdown_path = Path(temp_dir) / "review-summary.md"
        input_path.write_text(
            json.dumps(review_state, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        run_command(
            [
                "python3",
                str(script_path),
                "--input",
                str(input_path),
                "--markdown-out",
                str(markdown_path),
                "--publish-pr-comment",
            ],
            env=env,
        )


def ensure_allowed_review_recommendation(
    *,
    pr_number: str,
    workflow_name: str,
    review_state: dict[str, object],
    allowed_review_recommendations: list[str],
) -> None:
    recommendation = str(review_state.get("overall_recommendation") or "").strip().lower()
    if recommendation in allowed_review_recommendations:
        return
    raise RuntimeError(
        f"{workflow_name} recommendation for PR #{pr_number} was '{recommendation}', "
        f"expected one of {', '.join(allowed_review_recommendations)}.",
    )


def split_validation_workflows(
    profile: dict[str, object],
) -> tuple[dict[str, object] | None, list[dict[str, object]]]:
    workflows = profile_validation_workflows(profile)
    review_workflows = [
        workflow
        for workflow in workflows
        if str(workflow.get("review_state_script") or "")
    ]
    if len(review_workflows) > 1:
        raise ValueError("Only one validation workflow with review_state_script is supported.")

    review_workflow = review_workflows[0] if review_workflows else None
    other_workflows = [
        workflow
        for workflow in workflows
        if workflow is not review_workflow
    ]
    return review_workflow, other_workflows


def build_stabilize_prompt(
    *,
    profile: dict[str, object],
    context_root: Path,
    pr_number: str,
    repair_branch: str,
    source_run_id: str,
    workflow_name: str,
    review_state: dict[str, object],
) -> str:
    review_summary = str(review_state.get("summary") or "").strip() or "No summary provided."
    review_recommendation = str(review_state.get("overall_recommendation") or "").strip() or "unknown"
    review_run_id = str(review_state.get("run_id") or "").strip() or "unknown"
    prompt_context_files = render_bullet_list(profile_string_list(profile, "prompt_context_files"))
    validation_commands = render_bullet_list(profile_string_list(profile, "validation_commands"))
    review_state_json = json.dumps(review_state, indent=2, sort_keys=True)

    return render_markdown_template(
        "stabilize-goal.md.in",
        {
            CONTEXT_ROOT_TOKEN: context_root.as_posix(),
            DISPLAY_NAME_TOKEN: profile_string(profile, "display_name"),
            PR_NUMBER_TOKEN: pr_number,
            PROMPT_CONTEXT_FILES_TOKEN: prompt_context_files,
            REPAIR_BRANCH_TOKEN: repair_branch,
            REVIEW_RECOMMENDATION_TOKEN: review_recommendation,
            REVIEW_RUN_ID_TOKEN: review_run_id,
            REVIEW_STATE_JSON_TOKEN: review_state_json,
            REVIEW_SUMMARY_TOKEN: review_summary,
            REVIEW_WORKFLOW_NAME_TOKEN: workflow_name,
            SOURCE_RUN_ID_TOKEN: source_run_id,
            VALIDATION_COMMANDS_TOKEN: validation_commands,
        },
    )


def write_stabilization_context(
    *,
    context_root: Path,
    review_state: dict[str, object],
    prompt_text: str,
) -> Path:
    context_root.mkdir(parents=True, exist_ok=True)
    (context_root / "review-state.json").write_text(
        json.dumps(review_state, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    prompt_path = context_root / "stabilize-goal.md"
    prompt_path.write_text(prompt_text, encoding="utf-8")
    return prompt_path


def run_validation_commands(commands: list[str]) -> None:
    for command in commands:
        print(f"Running validation command: {command}")
        run_shell_command(command)


def commit_review_fix(
    *,
    pr_number: str,
    repair_branch: str,
    ticket_id: str,
    review_state: dict[str, object],
) -> str:
    push_token = os.environ.get("GH_TOKEN", "").strip()
    if not push_token:
        raise RuntimeError(
            "EXPKITS_AGENT_TOKEN must be provided as GH_TOKEN when pushing stabilization commits."
        )
    repository = os.environ.get("GITHUB_REPOSITORY", "").strip()
    if not repository:
        raise RuntimeError("GITHUB_REPOSITORY is required to push stabilization commits.")
    server_url = os.environ.get("GITHUB_SERVER_URL", "https://github.com").strip()
    parsed_server_url = urllib.parse.urlparse(server_url)
    if parsed_server_url.scheme != "https" or not parsed_server_url.netloc:
        raise RuntimeError(f"Unsupported GITHUB_SERVER_URL for token-authenticated push: {server_url}")
    user_payload = github_api_json("user")
    push_actor = str(user_payload.get("login") or "").strip() if isinstance(user_payload, dict) else ""
    if not push_actor:
        raise RuntimeError("Unable to resolve PAT owner login for stabilization push.")

    run_command(["git", "config", "user.name", "github-actions[bot]"])
    run_command(["git", "config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com"])
    run_command(
        [
            "git",
            "remote",
            "set-url",
            "origin",
            f"https://{push_actor}:{push_token}@{parsed_server_url.netloc}/{repository}.git",
        ]
    )
    run_command(["git", "add", "-A"])
    if run_command(["git", "diff", "--cached", "--quiet"], check=False).returncode == 0:
        return ""

    commit_command = [
        "git",
        "commit",
        "-m",
        f"[bot] Address Agent Review findings on PR #{pr_number}",
        "-m",
        f"Task: {ticket_id}",
    ]

    review_run_id = str(review_state.get("run_id") or "").strip()
    if review_run_id:
        commit_command.extend(["-m", f"Agent Review run: {review_run_id}"])

    review_summary = str(review_state.get("summary") or "").strip()
    if review_summary:
        commit_command.extend(["-m", review_summary])

    run_command(commit_command)
    run_command(["git", "push", "origin", f"HEAD:{repair_branch}"])
    return run_command(["git", "rev-parse", "HEAD"], capture_output=True).stdout.strip()


def command_resolve_pr_details(args: argparse.Namespace) -> int:
    write_outputs(read_pr_details(args.pr_number), args.github_output)
    return 0


def command_prepare_stabilization_context(args: argparse.Namespace) -> int:
    profile = load_profile(args.profile_path)
    context_root = Path(args.context_root)
    review_workflow, _ = split_validation_workflows(profile)
    if review_workflow is None:
        raise RuntimeError("Stabilization requires a validation workflow with review_state_script.")
    repository = os.environ.get("GITHUB_REPOSITORY", "")

    pr_details = read_pr_details(args.pr_number)
    repair_branch = pr_details["repair_branch"]
    head_sha = args.head_sha or pr_details["head_sha"]
    if not repair_branch or not head_sha:
        raise RuntimeError(f"Unable to resolve repair branch and head SHA for PR #{args.pr_number}.")

    review_state = read_review_state(
        state_script=str(review_workflow["review_state_script"]),
        pr_number=args.pr_number,
    )
    review_head_sha = str(review_state.get("head_sha") or "").strip()
    recommendation = str(review_state.get("overall_recommendation") or "").strip().lower()
    if (not recommendation or (review_head_sha and review_head_sha != head_sha)) and repository:
        review_run_id = find_latest_workflow_run_for_head(
            repository=repository,
            workflow_file=str(review_workflow["workflow_file"]),
            repair_branch=repair_branch,
            head_sha=head_sha,
        )
        if review_run_id:
            review_state = read_review_artifact_state(
                repository=repository,
                run_id=review_run_id,
                head_sha=head_sha,
            )
            review_head_sha = str(review_state.get("head_sha") or "").strip()
            recommendation = str(review_state.get("overall_recommendation") or "").strip().lower()
    if not recommendation:
        raise RuntimeError(f"Latest review state for PR #{args.pr_number} did not contain a recommendation.")
    if review_head_sha and review_head_sha != head_sha:
        raise RuntimeError(
            f"Latest review state head SHA {review_head_sha} did not match expected head SHA {head_sha} for PR #{args.pr_number}.",
        )

    write_stabilization_context(
        context_root=context_root,
        review_state=review_state,
        prompt_text=build_stabilize_prompt(
            profile=profile,
            context_root=context_root,
            pr_number=args.pr_number,
            repair_branch=repair_branch,
            source_run_id=args.source_run_id,
            workflow_name=str(review_workflow["workflow_name"]),
            review_state=review_state,
        ),
    )
    write_outputs(
        {
            "repair_branch": repair_branch,
            "head_sha": head_sha,
            "target_branch": pr_details["target_branch"],
            "review_recommendation": recommendation,
            "review_run_id": str(review_state.get("run_id") or "").strip(),
            "agent_model": profile_agent_model(profile, AgentInstance.STABILIZATION, args.profile_path),
        },
        args.github_output,
    )
    return 0


def command_run_validation(args: argparse.Namespace) -> int:
    profile = load_profile(args.profile_path)
    run_validation_commands(profile_string_list(profile, "validation_commands"))
    return 0


def command_commit_review_fix(args: argparse.Namespace) -> int:
    context_root = Path(args.context_root)
    review_state = read_json_file(context_root / "review-state.json")
    head_sha = commit_review_fix(
        pr_number=args.pr_number,
        repair_branch=args.repair_branch,
        ticket_id=args.ticket_id,
        review_state=review_state,
    )
    if not head_sha:
        raise RuntimeError(
            f"Agent produced no repository changes for PR #{args.pr_number} during stabilization.",
        )
    write_outputs({"head_sha": head_sha}, args.github_output)
    return 0


def command_stabilize_pr(args: argparse.Namespace) -> int:
    profile = load_profile(args.profile_path)
    repository = os.environ["GITHUB_REPOSITORY"]
    context_root = Path(args.context_root)
    dispatch_ref = os.environ.get("GITHUB_REF_NAME", "main")
    review_workflow, other_workflows = split_validation_workflows(profile)
    pr_details = read_pr_details(args.pr_number)
    repair_branch = pr_details["repair_branch"] or args.repair_branch
    target_branch = pr_details["target_branch"]
    head_sha = args.head_sha or pr_details["head_sha"]

    for attempt in range(1, STABILIZATION_MAX_ATTEMPTS + 1):
        print(f"Stabilization attempt {attempt}/{STABILIZATION_MAX_ATTEMPTS} for PR #{args.pr_number} at {head_sha}")
        dispatch_context = build_validation_dispatch_context(
            pr_number=args.pr_number,
            repair_branch=repair_branch,
            head_sha=head_sha,
            target_branch=target_branch,
            source_run_id=args.source_run_id,
            ticket_id=args.ticket_id,
        )

        if review_workflow is not None:
            review_run_id, review_run_event = ensure_validation_workflow_run(
                repository=repository,
                workflow=review_workflow,
                repair_branch=repair_branch,
                head_sha=head_sha,
                dispatch_context=dispatch_context,
            )
            review_state = wait_for_review_state(
                pr_number=args.pr_number,
                workflow_name=str(review_workflow["workflow_name"]),
                review_state_script=str(review_workflow["review_state_script"]),
                expected_run_id=review_run_id,
                head_sha=head_sha,
            )
            if review_run_event == "workflow_dispatch":
                publish_review_state_to_pr(
                    pr_number=args.pr_number,
                    head_sha=head_sha,
                    review_state=review_state,
                )
            allowed_recommendations = workflow_allowed_review_recommendations(review_workflow)
            recommendation = str(review_state.get("overall_recommendation") or "").strip().lower()
            if recommendation not in allowed_recommendations:
                dispatch_stabilizer_workflow(
                    repository=repository,
                    pr_number=args.pr_number,
                    head_sha=head_sha,
                    source_run_id=args.source_run_id,
                    ticket_id=args.ticket_id,
                    profile_path=args.profile_path,
                    context_root=str(context_root),
                    dispatch_ref=dispatch_ref,
                    dispatch_nonce=f"pr-{args.pr_number}-attempt-{attempt}-{int(time.time())}",
                )
                pr_details = read_pr_details(args.pr_number)
                repair_branch = pr_details["repair_branch"] or repair_branch
                target_branch = pr_details["target_branch"]
                head_sha = pr_details["head_sha"]
                # The stabilizer may have pushed a new commit. Restart the
                # loop so that the head gets a fresh Agent Review before merge.
                continue

            ensure_allowed_review_recommendation(
                pr_number=args.pr_number,
                workflow_name=str(review_workflow["workflow_name"]),
                review_state=review_state,
                allowed_review_recommendations=allowed_recommendations,
            )

        for workflow in other_workflows:
            ensure_validation_workflow_run(
                repository=repository,
                workflow=workflow,
                repair_branch=repair_branch,
                head_sha=head_sha,
                dispatch_context=dispatch_context,
            )

        merge_pr(args.pr_number)
        return 0

    raise RuntimeError(
        f"Exceeded {STABILIZATION_MAX_ATTEMPTS} stabilization attempts for PR #{args.pr_number}.",
    )


def command_wait_for_pr_workflows(args: argparse.Namespace) -> int:
    profile = load_profile(args.profile_path)
    repository = os.environ["GITHUB_REPOSITORY"]
    pr_details = read_pr_details(args.pr_number)
    dispatch_context = build_validation_dispatch_context(
        pr_number=args.pr_number,
        repair_branch=pr_details["repair_branch"] or args.repair_branch,
        head_sha=args.head_sha,
        target_branch=pr_details["target_branch"],
        source_run_id="",
        ticket_id="",
    )
    for workflow in profile_validation_workflows(profile):
        run_id, run_event = ensure_validation_workflow_run(
            repository=repository,
            workflow=workflow,
            repair_branch=pr_details["repair_branch"] or args.repair_branch,
            head_sha=args.head_sha,
            dispatch_context=dispatch_context,
        )
        review_state_script = str(workflow.get("review_state_script") or "")
        if review_state_script:
            review_state = wait_for_review_state(
                pr_number=args.pr_number,
                workflow_name=str(workflow["workflow_name"]),
                review_state_script=review_state_script,
                expected_run_id=run_id,
                head_sha=args.head_sha,
            )
            if run_event == "workflow_dispatch":
                publish_review_state_to_pr(
                    pr_number=args.pr_number,
                    head_sha=args.head_sha,
                    review_state=review_state,
                )
            ensure_allowed_review_recommendation(
                pr_number=args.pr_number,
                workflow_name=str(workflow["workflow_name"]),
                review_state=review_state,
                allowed_review_recommendations=workflow_allowed_review_recommendations(workflow),
            )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Workflow Action Update Agent helper utility.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    resolve_inputs = subparsers.add_parser("resolve-inputs")
    resolve_inputs.add_argument("--profile-path", default=default_profile_path_argument())
    resolve_inputs.add_argument("--source-run-id", default="")
    resolve_inputs.add_argument("--target-branch", default="")
    resolve_inputs.add_argument("--ticket-id", required=True)
    resolve_inputs.add_argument("--current-ref-name", default="")
    resolve_inputs.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    resolve_inputs.set_defaults(func=command_resolve_inputs)

    collect_context = subparsers.add_parser("collect-context")
    collect_context.add_argument("--context-root", default=".agent-workflows/workflow-action-update-agent")
    collect_context.add_argument("--source-run-id", required=True)
    collect_context.add_argument("--source-run-url", required=True)
    collect_context.add_argument("--source-workflow-name", required=True)
    collect_context.set_defaults(func=command_collect_context)

    build_markdown = subparsers.add_parser("build-markdown")
    build_markdown.add_argument("--profile-path", default=default_profile_path_argument())
    build_markdown.add_argument("--context-root", default=".agent-workflows/workflow-action-update-agent")
    build_markdown.add_argument("--source-run-id", required=True)
    build_markdown.add_argument("--source-run-url", required=True)
    build_markdown.add_argument("--source-workflow-name", required=True)
    build_markdown.add_argument("--target-branch", required=True)
    build_markdown.add_argument("--repair-branch", required=True)
    build_markdown.add_argument("--ticket-id", required=True)
    build_markdown.set_defaults(func=command_build_markdown)

    package_patch = subparsers.add_parser("package-patch")
    package_patch.add_argument("--patch-file", required=True)
    package_patch.add_argument("--diffstat-file", required=True)
    package_patch.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    package_patch.set_defaults(func=command_package_patch)

    require_generated_patch = subparsers.add_parser("require-generated-patch")
    require_generated_patch.add_argument("--source-run-id", default="")
    require_generated_patch.set_defaults(func=command_require_generated_patch)

    apply_patch_and_push = subparsers.add_parser("apply-patch-and-push")
    apply_patch_and_push.add_argument("--profile-path", default=default_profile_path_argument())
    apply_patch_and_push.add_argument("--patch-root", required=True)
    apply_patch_and_push.add_argument("--repair-branch", required=True)
    apply_patch_and_push.add_argument("--source-run-id", required=True)
    apply_patch_and_push.add_argument("--source-run-url", required=True)
    apply_patch_and_push.add_argument("--source-workflow-name", required=True)
    apply_patch_and_push.add_argument("--ticket-id", required=True)
    apply_patch_and_push.add_argument("--body-file", required=True)
    apply_patch_and_push.add_argument("--pr-title-file", required=True)
    apply_patch_and_push.add_argument("--commit-subject-file", required=True)
    apply_patch_and_push.add_argument("--commit-notes-file", required=True)
    apply_patch_and_push.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    apply_patch_and_push.set_defaults(func=command_apply_patch_and_push)

    create_draft_pr = subparsers.add_parser("create-draft-pr")
    create_draft_pr.add_argument("--profile-path", default=default_profile_path_argument())
    create_draft_pr.add_argument("--body-file", required=True)
    create_draft_pr.add_argument("--pr-title-file", required=True)
    create_draft_pr.add_argument("--target-branch", required=True)
    create_draft_pr.add_argument("--repair-branch", required=True)
    create_draft_pr.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    create_draft_pr.set_defaults(func=command_create_draft_pr)

    merge_pr = subparsers.add_parser("merge-pr")
    merge_pr.add_argument("--pr-number", required=True)
    merge_pr.set_defaults(func=command_merge_pr)

    resolve_pr_details = subparsers.add_parser("resolve-pr-details")
    resolve_pr_details.add_argument("--pr-number", required=True)
    resolve_pr_details.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    resolve_pr_details.set_defaults(func=command_resolve_pr_details)

    prepare_stabilization_context = subparsers.add_parser("prepare-stabilization-context")
    prepare_stabilization_context.add_argument(
        "--profile-path",
        default=default_profile_path_argument(),
    )
    prepare_stabilization_context.add_argument("--pr-number", required=True)
    prepare_stabilization_context.add_argument("--head-sha", default="")
    prepare_stabilization_context.add_argument("--source-run-id", default="")
    prepare_stabilization_context.add_argument(
        "--context-root",
        default=".agent-workflows/workflow-action-update-agent",
    )
    prepare_stabilization_context.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    prepare_stabilization_context.set_defaults(func=command_prepare_stabilization_context)

    run_validation = subparsers.add_parser("run-validation")
    run_validation.add_argument("--profile-path", default=default_profile_path_argument())
    run_validation.set_defaults(func=command_run_validation)

    commit_review_fix_parser = subparsers.add_parser("commit-review-fix")
    commit_review_fix_parser.add_argument("--context-root", default=".agent-workflows/workflow-action-update-agent")
    commit_review_fix_parser.add_argument("--pr-number", required=True)
    commit_review_fix_parser.add_argument("--repair-branch", required=True)
    commit_review_fix_parser.add_argument("--ticket-id", required=True)
    commit_review_fix_parser.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    commit_review_fix_parser.set_defaults(func=command_commit_review_fix)

    stabilize_pr = subparsers.add_parser("stabilize-pr")
    stabilize_pr.add_argument("--profile-path", default=default_profile_path_argument())
    stabilize_pr.add_argument("--pr-number", required=True)
    stabilize_pr.add_argument("--repair-branch", required=True)
    stabilize_pr.add_argument("--head-sha", required=True)
    stabilize_pr.add_argument("--ticket-id", required=True)
    stabilize_pr.add_argument("--source-run-id", required=True)
    stabilize_pr.add_argument("--context-root", default=".agent-workflows/workflow-action-update-agent")
    stabilize_pr.set_defaults(func=command_stabilize_pr)

    wait_for_workflows = subparsers.add_parser("wait-for-pr-workflows")
    wait_for_workflows.add_argument("--profile-path", default=default_profile_path_argument())
    wait_for_workflows.add_argument("--pr-number", required=True)
    wait_for_workflows.add_argument("--repair-branch", required=True)
    wait_for_workflows.add_argument("--head-sha", required=True)
    wait_for_workflows.set_defaults(func=command_wait_for_pr_workflows)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return int(args.func(args))
    except subprocess.CalledProcessError as exc:
        if exc.stdout:
            sys.stdout.write(exc.stdout)
        if exc.stderr:
            sys.stderr.write(exc.stderr)
        return exc.returncode
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError, re.error) as exc:
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
