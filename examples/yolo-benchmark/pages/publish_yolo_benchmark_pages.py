#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
# Updates the persisted YOLO benchmark report site used by GitHub Pages.
################################################################

from __future__ import annotations

import base64
import datetime as dt
import html
import json
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
from string import Template
from pathlib import Path
from typing import Any, Callable
from urllib.parse import quote


SCRIPT_DIR = Path(__file__).resolve().parent
ASSET_DIR = SCRIPT_DIR / "assets"
TEMPLATE_DIR = SCRIPT_DIR / "templates"
REPO_ROOT = SCRIPT_DIR.parents[2]
REPORT_ROOT = "yolo-benchmark"
ARTIFACT_ROOT_NAME = REPORT_ROOT
PRODUCT_TITLE = "Arm Perception kit"
INDEX_HTML = "index.html"
REPORT_INDEX_META = "report-index-meta.txt"
PERCENTILE_METRICS = ("p50_ms", "p75_ms", "p95_ms", "p99_ms")
RUN_METRICS = ("avg_ms", *PERCENTILE_METRICS)


class PublishError(RuntimeError):
    pass


def usage() -> None:
    print("Usage: examples/yolo-benchmark/pages/publish.sh publish|cleanup", file=sys.stderr)


def env(name: str, default: str = "") -> str:
    return os.environ.get(name, default)


def require_env(name: str) -> str:
    value = env(name)
    if not value:
        raise PublishError(f"{name} is required.")
    return value


def html_escape(value: object) -> str:
    return html.escape(str(value), quote=True)


def html_anchor(href: str, text: str) -> str:
    return f'<a href="{html_escape(href)}">{html_escape(text)}</a>'


def render_template(name: str, values: dict[str, object]) -> str:
    template = Template((TEMPLATE_DIR / name).read_text(encoding="utf-8"))
    return template.substitute({key: str(value) for key, value in values.items()})


def render_page(title: str, css_href: str, body: str) -> str:
    return render_template(
        "base.html.in",
        {
            "title": html_escape(title),
            "css_href": html_escape(css_href),
            "body": body,
        },
    )


def tooltip_attrs(text: str) -> str:
    escaped = html_escape(text)
    return f' data-tooltip="{escaped}" aria-label="{escaped}"'


def resolve_repo_path(path_text: str) -> Path | None:
    if not path_text:
        return None
    path = Path(path_text)
    candidates = [path] if path.is_absolute() else [REPO_ROOT / path]
    if path_text.startswith("/work/"):
        candidates.append(REPO_ROOT / path_text.removeprefix("/work/"))
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def describe_opchain_ops(opchain_path: str) -> list[str]:
    path = resolve_repo_path(opchain_path)
    if path is None:
        return []
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return []
    ops = doc.get("ops")
    if not isinstance(ops, list):
        return []
    descriptions = []
    for op in ops:
        if not isinstance(op, dict):
            continue
        op_id = op.get("id")
        if not isinstance(op_id, str):
            continue
        attrs = op.get("attributes")
        details = []
        if isinstance(attrs, dict):
            for key in ("parser", "applyNms", "confidenceThreshold", "iouThreshold"):
                if key in attrs:
                    details.append(f"{key}={attrs[key]}")
        suffix = f" ({', '.join(details)})" if details else ""
        descriptions.append(f"{op_id}{suffix}")
    return descriptions


def dry_run_enabled() -> bool:
    return env("YOLO_PAGES_DRY_RUN") == "1"


def set_output(name: str, value: str) -> None:
    output = env("GITHUB_OUTPUT")
    if output:
        with open(output, "a", encoding="utf-8") as handle:
            handle.write(f"{name}={value}\n")


def run(args: list[str], cwd: Path | None = None, quiet: bool = False) -> subprocess.CompletedProcess:
    stdout = subprocess.DEVNULL if quiet else None
    stderr = subprocess.DEVNULL if quiet else None
    return subprocess.run(args, cwd=cwd, check=True, stdout=stdout, stderr=stderr, text=True)


def run_maybe(args: list[str], cwd: Path | None = None, quiet: bool = False) -> subprocess.CompletedProcess:
    stdout = subprocess.DEVNULL if quiet else None
    stderr = subprocess.DEVNULL if quiet else None
    return subprocess.run(args, cwd=cwd, check=False, stdout=stdout, stderr=stderr, text=True)


def git_auth_header() -> str:
    token = require_env("GITHUB_TOKEN")
    return base64.b64encode(f"x-access-token:{token}".encode("utf-8")).decode("ascii")


def git_with_auth(args: list[str], site_dir: Path, auth_header: str, quiet: bool = False) -> subprocess.CompletedProcess:
    return run(
        [
            "git",
            "-C",
            str(site_dir),
            "-c",
            f"http.https://github.com/.extraheader=AUTHORIZATION: basic {auth_header}",
            *args,
        ],
        quiet=quiet,
    )


def git_with_auth_maybe(args: list[str], site_dir: Path, auth_header: str,
                        quiet: bool = False) -> subprocess.CompletedProcess:
    stdout = subprocess.DEVNULL if quiet else None
    stderr = subprocess.DEVNULL if quiet else None
    return subprocess.run(
        [
            "git",
            "-C",
            str(site_dir),
            "-c",
            f"http.https://github.com/.extraheader=AUTHORIZATION: basic {auth_header}",
            *args,
        ],
        check=False,
        stdout=stdout,
        stderr=stderr,
        text=True,
    )


def checkout_site_branch(site_dir: Path, storage_branch: str) -> None:
    if dry_run_enabled():
        if site_dir.exists():
            shutil.rmtree(site_dir)
        site_dir.mkdir(parents=True)
        run(["git", "-C", str(site_dir), "init", "-b", storage_branch], quiet=True)
        run(["git", "-C", str(site_dir), "config", "user.name", "local-yolo-pages"])
        run(["git", "-C", str(site_dir), "config", "user.email", "local@example.invalid"])
        return

    repository = require_env("GITHUB_REPOSITORY")
    auth_header = git_auth_header()
    print(f"::add-mask::{auth_header}")

    if site_dir.exists():
        shutil.rmtree(site_dir)
    site_dir.mkdir(parents=True)

    run(["git", "-C", str(site_dir), "init"])
    run(["git", "-C", str(site_dir), "remote", "add", "origin", f"https://github.com/{repository}.git"])
    fetched = git_with_auth_maybe(["fetch", "--depth=1", "origin", storage_branch], site_dir, auth_header, quiet=True)
    if fetched.returncode == 0:
        run(["git", "-C", str(site_dir), "checkout", "-B", storage_branch, "FETCH_HEAD"])
    else:
        run(["git", "-C", str(site_dir), "checkout", "--orphan", storage_branch])
        run_maybe(["git", "-C", str(site_dir), "rm", "-rf", "."], quiet=True)

    run(["git", "-C", str(site_dir), "config", "user.name", "github-actions[bot]"])
    run(
        [
            "git",
            "-C",
            str(site_dir),
            "config",
            "user.email",
            "41898282+github-actions[bot]@users.noreply.github.com",
        ]
    )


def push_site_branch(site_dir: Path, storage_branch: str) -> bool:
    run(["git", "-C", str(site_dir), "add", "-A", "."])
    diff = run_maybe(["git", "-C", str(site_dir), "diff", "--cached", "--quiet"])
    if diff.returncode == 0:
        return False

    run(["git", "-C", str(site_dir), "commit", "-m", "Update YOLO benchmark report pages"])
    if dry_run_enabled():
        print(f"Dry-run: generated YOLO benchmark Pages site at {site_dir}")
        return True

    auth_header = git_auth_header()
    git_with_auth(["push", "origin", f"HEAD:{storage_branch}"], site_dir, auth_header)
    return True


def copy_asset(site_dir: Path, name: str) -> None:
    source = ASSET_DIR / name
    if not source.is_file():
        raise PublishError(f"Missing YOLO Pages asset: {source}")
    destination = site_dir / REPORT_ROOT / name
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)


def write_index_assets(site_dir: Path) -> None:
    copy_asset(site_dir, "report-index.css")


def read_first_line(path: Path, default: str) -> str:
    if not path.is_file():
        return default
    lines = path.read_text(encoding="utf-8").splitlines()
    return lines[0] if lines else default


def pr_report_title(pr_number: str, repository: str) -> str:
    if repository:
        result = subprocess.run(
            ["gh", "pr", "view", pr_number, "--repo", repository, "--json", "title", "--jq", ".title"],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        title = result.stdout.strip()
        if result.returncode == 0 and title:
            return f"PR #{pr_number} - {title}"
    return f"PR #{pr_number}"


def rel_to_report_root(target: Path, site_dir: Path) -> str:
    depth = len(target.relative_to(site_dir / REPORT_ROOT).parts)
    return "../" * depth


def build_source_meta_html(repository: str, branch: str, head_sha: str, run_id: str, run_attempt: str) -> str:
    repo_url = f"https://github.com/{repository}"
    branch_url = quote(branch, safe="")
    return "".join(
        [
            html_anchor(f"{repo_url}/tree/{branch_url}", branch),
            " @ ",
            html_anchor(f"{repo_url}/commit/{head_sha}", head_sha[:12]),
            " | ",
            html_anchor(f"{repo_url}/actions/runs/{run_id}", f"run {run_id}"),
            f" attempt {html_escape(run_attempt)}",
        ]
    )


def build_report_meta_html(
    repository: str,
    event: str,
    pr_number: str,
    branch: str,
    head_sha: str,
    run_id: str,
    run_attempt: str,
) -> str:
    if event == "pull_request":
        prefix = html_anchor(f"https://github.com/{repository}/pull/{pr_number}", f"PR #{pr_number}")
    elif event == "workflow_dispatch":
        prefix = "Manual"
    else:
        prefix = "Nightly"
    return f"{prefix} | {build_source_meta_html(repository, branch, head_sha, run_id, run_attempt)}"


def build_report_index_meta_text(
    branch: str,
    head_sha: str,
    run_id: str,
    run_attempt: str,
    now: dt.datetime | None = None,
) -> str:
    if now is None:
        now = dt.datetime.now(dt.timezone.utc)
    return f"{branch} @ {head_sha[:12]} | run {run_id} attempt {run_attempt} | {now.strftime('%b %d, %Y %H:%M UTC')}"


def parse_github_time(value: str) -> dt.datetime:
    return dt.datetime.fromisoformat(value.replace("Z", "+00:00"))


def should_prune_closed_pr(state: str, closed_at: str, cutoff: dt.datetime) -> bool:
    if state == "OPEN" or not closed_at:
        return False
    return parse_github_time(closed_at) <= cutoff


def pr_state(repository: str, pr_number: str) -> dict | None:
    result = subprocess.run(
        ["gh", "pr", "view", pr_number, "--repo", repository, "--json", "state,closedAt"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    if result.returncode != 0 or not result.stdout.strip():
        return None
    return json.loads(result.stdout)


def parse_retention_days(value: str) -> int:
    try:
        days = int(value)
    except ValueError as error:
        raise PublishError(f"YOLO_PAGES_RETENTION_DAYS must be an integer: {value}") from error
    if days < 0:
        raise PublishError("YOLO_PAGES_RETENTION_DAYS must be zero or greater.")
    return days


def local_artifact_ignore(root: Path):
    root = root.resolve()

    def ignore(directory: str, names: list[str]) -> set[str]:
        ignored = {name for name in names if name in {"__pycache__", "predictions.jsonl"}}
        if Path(directory).resolve() == root:
            ignored.update(name for name in names if name not in {"images.tsv", "runs"})
        return ignored

    return ignore


def download_report_artifact(artifact_dir: Path, repository: str, run_id: str, run_attempt: str) -> bool:
    local_artifact_dir = env("YOLO_PAGES_LOCAL_ARTIFACT_DIR")
    if local_artifact_dir:
        source = Path(local_artifact_dir)
        if not source.is_dir():
            raise PublishError(f"Local YOLO benchmark artifact not found: {source}")
        shutil.copytree(source, artifact_dir / "local-artifact" / ARTIFACT_ROOT_NAME,
                        ignore=local_artifact_ignore(source))
        return True

    artifact_name = f"yolo-benchmark-{run_id}-{run_attempt}"
    result = subprocess.run(
        ["gh", "run", "download", run_id, "--repo", repository, "--name", artifact_name, "--dir", str(artifact_dir)],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    if result.returncode == 0:
        return True
    print(f"No YOLO benchmark artifact found for run {run_id} attempt {run_attempt}.")
    return False


def find_yolo_artifact(artifact_dir: Path) -> Path | None:
    if (artifact_dir / "runs").is_dir():
        return artifact_dir
    for path in artifact_dir.rglob(ARTIFACT_ROOT_NAME):
        if path.is_dir() and (path / "runs").is_dir():
            return path
    for runs_dir in artifact_dir.rglob("runs"):
        if runs_dir.is_dir():
            return runs_dir.parent
    return None


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def load_report_runs(artifact_root: Path) -> list[dict[str, Any]]:
    runs_dir = artifact_root / "runs"
    if not runs_dir.is_dir():
        raise PublishError(f"YOLO artifact is missing runs directory: {artifact_root}")

    runs = []
    for run_dir in sorted(path for path in runs_dir.iterdir() if path.is_dir()):
        comparison_path = run_dir / "comparison.json"
        if not comparison_path.is_file():
            continue
        comparison = load_json(comparison_path)
        runs.append({"name": run_dir.name, "path": run_dir, "comparison": comparison})
    if not runs:
        raise PublishError(f"YOLO artifact contains no run comparison JSON files: {artifact_root}")
    return runs


def metric_value(run: dict[str, Any], metric: str, key: str) -> float:
    return float(run["comparison"]["timing_delta"]["per_image_ms"][metric][key])


def report_run_metrics(_runs: list[dict[str, Any]]) -> tuple[str, ...]:
    return RUN_METRICS


def report_percentile_metrics(_runs: list[dict[str, Any]]) -> tuple[str, ...]:
    return PERCENTILE_METRICS


def format_ms(value: float) -> str:
    return f"{value:.1f}"


def format_bar_label(value: float) -> str:
    return f"{value:.1f}"


def run_delta(run: dict[str, Any], metric: str) -> dict[str, Any]:
    return run["comparison"]["timing_delta"]["per_image_ms"][metric]


def result_label(delta: dict[str, Any]) -> str:
    delta_percent = delta.get("delta_percent")
    if delta_percent is None:
        return '<span class="verdict verdict-neutral">n/a</span>'
    percent = float(delta_percent)
    if abs(percent) < 0.05:
        return '<span class="verdict verdict-neutral">PEK equal</span>'
    if percent < 0:
        return f'<span class="verdict verdict-fast">PEK faster by {abs(percent):.1f}%</span>'
    return f'<span class="verdict verdict-slow">Bare faster by {percent:.1f}%</span>'


def write_selected_artifacts(artifact_root: Path, target: Path, runs: list[dict[str, Any]]) -> None:
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)

    image_list = artifact_root / "images.tsv"
    if image_list.is_file():
        shutil.copy2(image_list, target / "images.tsv")

    runs_target = target / "runs"
    for run in runs:
        source = run["path"]
        destination = runs_target / run["name"]
        destination.mkdir(parents=True)
        for relative in (
            Path("comparison.json"),
            Path("comparison.md"),
            Path("bare") / "benchmark_summary.json",
            Path("pek") / "benchmark_summary.json",
        ):
            src = source / relative
            if src.is_file():
                dst = destination / relative
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dst)


def chart_svg_header(title: str, width: int) -> str:
    legend_x = width - 152
    return (
        f'<text class="chart-svg-title" x="18" y="38">{html_escape(title)}</text>'
        f'<g class="chart-svg-legend" transform="translate({legend_x},34)">'
        '<circle class="chart-bare" cx="0" cy="0" r="5"></circle><text x="12" y="5">Bare</text>'
        '<circle class="chart-pek" cx="72" cy="0" r="5"></circle><text x="84" y="5">PEK</text>'
        "</g>"
    )


def line_chart_svg(
    labels: list[str],
    bare_values: list[float],
    pek_values: list[float],
    title: str,
    aria_label: str,
) -> str:
    width = 960
    height = 540
    left = 176
    right = 32
    top = 104
    bottom = 118
    plot_width = width - left - right
    plot_height = height - top - bottom
    values = [*bare_values, *pek_values]
    min_value = min(values)
    max_value = max(values)
    padding = max((max_value - min_value) * 0.15, max_value * 0.02, 1.0)
    axis_min = max(0.0, min_value - padding)
    axis_max = max_value + padding
    axis_span = max(axis_max - axis_min, 1.0)
    step_count = 4

    def x(index: int) -> float:
        if len(labels) == 1:
            return left + plot_width / 2
        return left + (plot_width * index / (len(labels) - 1))

    def y(value: float) -> float:
        return top + plot_height - ((value - axis_min) / axis_span * plot_height)

    bare_points = [(x(index), y(value)) for index, value in enumerate(bare_values)]
    pek_points = [(x(index), y(value)) for index, value in enumerate(pek_values)]

    def path(points: list[tuple[float, float]]) -> str:
        return " ".join(
            f"{'M' if index == 0 else 'L'} {point_x:.1f},{point_y:.1f}"
            for index, (point_x, point_y) in enumerate(points)
        )

    parts = [
        f'<svg class="metric-chart" viewBox="0 0 {width} {height}" role="img" '
        f'aria-label="{html_escape(aria_label)}">',
        chart_svg_header(title, width),
        '<g class="chart-grid">',
    ]
    for step in range(step_count + 1):
        value = axis_min + axis_span * step / step_count
        grid_y = y(value)
        parts.append(f'<line x1="{left}" y1="{grid_y:.1f}" x2="{width - right}" y2="{grid_y:.1f}"></line>')
        parts.append(
            f'<text class="chart-y-tick" x="{left - 26}" y="{grid_y + 6:.1f}">{format_ms(value)}</text>'
        )
    parts.append("</g>")
    parts.append(f'<path class="chart-line chart-bare" d="{path(bare_points)}"></path>')
    parts.append(f'<path class="chart-line chart-pek" d="{path(pek_points)}"></path>')
    title_metric = aria_label.split(" ", 1)[0]
    for index, label in enumerate(labels):
        run_x = x(index)
        bare_y = bare_points[index][1]
        pek_y = pek_points[index][1]
        bare_tooltip = f"Bare {title_metric} {label}: {format_ms(bare_values[index])} ms"
        pek_tooltip = f"PEK {title_metric} {label}: {format_ms(pek_values[index])} ms"
        parts.append(
            f'<circle class="chart-point chart-bare" cx="{run_x:.1f}" cy="{bare_y:.1f}" r="4"'
            f'{tooltip_attrs(bare_tooltip)}></circle>'
            f'<circle class="chart-hit-area" cx="{run_x:.1f}" cy="{bare_y:.1f}" r="10"'
            f'{tooltip_attrs(bare_tooltip)}></circle>'
        )
        parts.append(
            f'<circle class="chart-point chart-pek" cx="{run_x:.1f}" cy="{pek_y:.1f}" r="4"'
            f'{tooltip_attrs(pek_tooltip)}></circle>'
            f'<circle class="chart-hit-area" cx="{run_x:.1f}" cy="{pek_y:.1f}" r="10"'
            f'{tooltip_attrs(pek_tooltip)}></circle>'
        )
        parts.append(
            f'<text class="chart-x-label" x="{run_x:.1f}" y="{height - 46}">{html_escape(label)}</text>'
        )
    parts.append(
        f'<text class="chart-axis-label" x="{left + plot_width / 2:.1f}" '
        f'y="{height - 14}">Run</text>'
    )
    parts.append(
        f'<text class="chart-axis-label" transform="rotate(-90)" '
        f'x="{-(top + plot_height / 2):.1f}" y="54">Time [ms]</text>'
    )
    parts.append("</svg>")
    return "".join(parts)


def chart_tooltip_html() -> str:
    return """      <div class="chart-tooltip" role="tooltip" hidden></div>
      <script>
(() => {
  const tooltip = document.querySelector(".chart-tooltip");
  if (!tooltip) return;
  const targetFor = event => event.target instanceof Element ? event.target.closest("[data-tooltip]") : null;
  const move = event => {
    const gap = 12;
    const pad = 8;
    const rect = tooltip.getBoundingClientRect();
    let left = event.clientX + gap;
    let top = event.clientY + gap;
    if (left + rect.width > window.innerWidth - pad) left = event.clientX - rect.width - gap;
    if (top + rect.height > window.innerHeight - pad) top = event.clientY - rect.height - gap;
    tooltip.style.left = `${Math.max(pad, left)}px`;
    tooltip.style.top = `${Math.max(pad, top)}px`;
  };
  document.addEventListener("pointerover", event => {
    const target = targetFor(event);
    if (!target) return;
    tooltip.textContent = target.dataset.tooltip;
    tooltip.hidden = false;
    move(event);
  });
  document.addEventListener("pointermove", event => {
    if (!tooltip.hidden) move(event);
  });
  document.addEventListener("pointerout", event => {
    if (targetFor(event)) tooltip.hidden = true;
  });
  document.addEventListener("click", event => {
    const button = event.target instanceof Element ? event.target.closest(".section-toggle") : null;
    if (!button) return;
    const content = document.getElementById(button.getAttribute("aria-controls"));
    if (!content) return;
    const expanded = button.getAttribute("aria-expanded") === "true";
    button.setAttribute("aria-expanded", expanded ? "false" : "true");
    content.hidden = expanded;
  });
  const activateRun = input => {
    const tabs = input.closest(".run-tabs");
    if (!tabs) return false;
    const panelId = input.getAttribute("aria-controls");
    tabs.querySelectorAll(".run-tab-label").forEach(label => {
      const control = label.querySelector(".run-tab-input");
      label.classList.toggle("is-active", control === input);
    });
    tabs.querySelectorAll(".run-tab-panel").forEach(panel => {
      panel.classList.toggle("is-active", panel.id === panelId);
    });
    const link = tabs.querySelector(".run-comparison-link");
    if (link) link.href = input.dataset.comparisonHref;
    return true;
  };
  const activateMetric = input => {
    const section = input.closest(".section-card");
    if (!section) return false;
    const panelId = input.getAttribute("aria-controls");
    section.querySelectorAll(".metric-tab-label").forEach(label => {
      const control = label.querySelector(".metric-tab-input");
      label.classList.toggle("is-active", control === input);
    });
    section.querySelectorAll(".metric-tab-panel").forEach(panel => {
      panel.classList.toggle("is-active", panel.id === panelId || panel.dataset.metricPanel === panelId);
    });
    return true;
  };
  document.addEventListener("change", event => {
    const input = event.target instanceof Element ? event.target.closest(".run-tab-input") : null;
    if (input) {
      activateRun(input);
      return;
    }
    const metricInput = event.target instanceof Element ? event.target.closest(".metric-tab-input") : null;
    if (metricInput) activateMetric(metricInput);
  });
  document.addEventListener("keydown", event => {
    if (event.key !== "ArrowLeft" && event.key !== "ArrowRight") return;
    if (event.altKey || event.ctrlKey || event.metaKey || event.shiftKey) return;
    const target = event.target instanceof Element ? event.target : null;
    if (target?.closest("input, textarea, select, [contenteditable=true]")) return;
    const tabs = document.querySelector(".run-tabs");
    if (!tabs) return;
    const inputs = Array.from(tabs.querySelectorAll(".run-tab-input"));
    const current = inputs.findIndex(input => input.checked);
    if (current < 0) return;
    const next = (current + (event.key === "ArrowRight" ? 1 : -1) + inputs.length) % inputs.length;
    inputs[next].checked = true;
    if (activateRun(inputs[next])) event.preventDefault();
  });
})();
      </script>
"""


def metric_trend_chart_svg(runs: list[dict[str, Any]], metric: str) -> str:
    return line_chart_svg(
        [run["name"].replace("run-", "", 1) for run in runs],
        [metric_value(run, metric, "bare_ms") for run in runs],
        [metric_value(run, metric, "pek_ms") for run in runs],
        f"{metric} trend",
        f"{metric} trend across runs",
    )


def stability_metric_tab_id(metric: str) -> str:
    return f"stability-metric-{metric.replace('_', '-')}"


def write_stability_charts(runs: list[dict[str, Any]]) -> str:
    parts = ["<div>"]
    metrics = report_percentile_metrics(runs)
    for metric in metrics:
        active = " is-active" if metric == metrics[0] else ""
        parts.append(
            f'<div class="metric-tab-panel{active}" id="{stability_metric_tab_id(metric)}-panel">'
            f"{metric_trend_chart_svg(runs, metric)}</div>\n"
        )
    parts.append("</div>")
    return "".join(parts)


def write_stability_metric_controls(runs: list[dict[str, Any]]) -> str:
    parts = ['<div class="metric-tab-controls">']
    for index, metric in enumerate(report_percentile_metrics(runs)):
        checked = " checked" if index == 0 else ""
        active = " is-active" if index == 0 else ""
        tab_id = stability_metric_tab_id(metric)
        panel_id = f"{tab_id}-panel"
        parts.append(
            f'<label class="metric-tab-label{active}"><input class="metric-tab-input" type="radio" '
            f'name="stability-metric" id="{tab_id}" aria-controls="{panel_id}"{checked}>'
            f'{html_escape(metric)}</label>'
        )
    parts.append("</div>")
    return "".join(parts)


def median_delta(runs: list[dict[str, Any]], metric: str) -> dict[str, float]:
    bare_values = [metric_value(run, metric, "bare_ms") for run in runs]
    pek_values = [metric_value(run, metric, "pek_ms") for run in runs]
    bare_ms = statistics.median(bare_values)
    pek_ms = statistics.median(pek_values)
    delta_ms = pek_ms - bare_ms
    ratio = None if bare_ms == 0 else pek_ms / bare_ms
    delta_percent = None if ratio is None else (ratio - 1.0) * 100.0
    return {
        "bare_ms": bare_ms,
        "pek_ms": pek_ms,
        "delta_ms": delta_ms,
        "ratio": ratio,
        "delta_percent": delta_percent,
    }


def overall_result_label(runs: list[dict[str, Any]]) -> str:
    return result_label(median_delta(runs, "avg_ms"))


def overall_result_block(runs: list[dict[str, Any]]) -> str:
    return f'<div class="report-overall"><span>Overall</span>{overall_result_label(runs)}</div>'


def write_delta_rows(deltas: list[tuple[str, dict[str, Any]]]) -> str:
    parts = []
    for metric, delta in deltas:
        parts.append(
            "<tr>"
            f"<td>{html_escape(metric)}</td>"
            f"<td>{format_ms(float(delta['bare_ms']))}</td>"
            f"<td>{format_ms(float(delta['pek_ms']))}</td>"
            f"<td>{format_ms(float(delta['delta_ms']))}</td>"
            f"<td>{result_label(delta)}</td>"
            "</tr>"
        )
    return "".join(parts)


def th(label: str, unit: str = "") -> str:
    unit_html = f'<span class="unit">{html_escape(unit)}</span>' if unit else ""
    return f'<th scope="col">{html_escape(label)}{unit_html}</th>'


def write_summary_table(runs: list[dict[str, Any]]) -> str:
    parts = [
        '<div class="table-scroll"><table class="benchmark-table">',
        "<thead><tr>",
        th("Metric"),
        th("Bare med", "[ms]"),
        th("PEK med", "[ms]"),
        th("Delta", "[ms]"),
        th("Result"),
        "</tr></thead><tbody>",
    ]
    parts.append(write_delta_rows([(metric, median_delta(runs, metric)) for metric in report_run_metrics(runs)]))
    parts.append("</tbody></table></div>")
    return "".join(parts)


def stability_range_cell(runs: list[dict[str, Any]], metric: str, key: str) -> str:
    values = [metric_value(run, metric, key) for run in runs]
    return f"{format_ms(min(values))}-{format_ms(max(values))} ({format_ms(max(values) - min(values))})"


def write_stability_table(runs: list[dict[str, Any]]) -> str:
    parts = [
        '<div class="table-scroll"><table class="benchmark-table">',
        "<thead><tr>",
        th("Metric"),
        th("Bare range", "[ms]"),
        th("PEK range", "[ms]"),
        "</tr></thead><tbody>",
    ]
    for metric in report_percentile_metrics(runs):
        parts.append(
            "<tr>"
            f"<td>{html_escape(metric)}</td>"
            f"<td>{stability_range_cell(runs, metric, 'bare_ms')}</td>"
            f"<td>{stability_range_cell(runs, metric, 'pek_ms')}</td>"
            "</tr>"
        )
    parts.append("</tbody></table></div>")
    return "".join(parts)


def write_run_table(run: dict[str, Any]) -> str:
    parts = [
        '<div class="table-scroll"><table class="benchmark-table">',
        "<thead><tr>",
        th("Metric"),
        th("Bare", "[ms]"),
        th("PEK", "[ms]"),
        th("Delta", "[ms]"),
        th("Result"),
        "</tr></thead><tbody>",
    ]
    parts.append(write_delta_rows([(metric, run_delta(run, metric)) for metric in report_run_metrics([run])]))
    parts.append("</tbody></table></div>")
    return "".join(parts)


def bar_chart_svg(
    metrics: tuple[str, ...],
    bare_value: Callable[[str], float],
    pek_value: Callable[[str], float],
    aria_label: str,
) -> str:
    width = 960
    height = 540
    left = 176
    right = 32
    top = 108
    bottom = 118
    plot_width = width - left - right
    plot_height = height - top - bottom
    values = [value(metric) for metric in metrics for value in (bare_value, pek_value)]
    min_value = min(values)
    max_value = max(values)
    padding = max((max_value - min_value) * 0.15, max_value * 0.02, 1.0)
    axis_min = max(0.0, min_value - padding)
    axis_max = max_value + padding
    axis_span = max(axis_max - axis_min, 1.0)
    step_count = 4
    group_width = plot_width / len(metrics)
    bar_width = min(50.0, group_width * 0.28)
    gap = 8.0

    def y(value: float) -> float:
        return top + plot_height - ((value - axis_min) / axis_span * plot_height)

    parts = [
        f'<svg class="metric-chart" viewBox="0 0 {width} {height}" role="img" '
        f'aria-label="{html_escape(aria_label)}">',
        chart_svg_header("Metric comparison", width),
        '<g class="chart-grid">',
    ]
    for step in range(step_count + 1):
        value = axis_min + axis_span * step / step_count
        grid_y = y(value)
        parts.append(f'<line x1="{left}" y1="{grid_y:.1f}" x2="{width - right}" y2="{grid_y:.1f}"></line>')
        parts.append(
            f'<text class="chart-y-tick" x="{left - 26}" y="{grid_y + 6:.1f}">{format_ms(value)}</text>'
        )
    parts.append("</g>")
    for index, metric in enumerate(metrics):
        center = left + group_width * (index + 0.5)
        bare = bare_value(metric)
        pek = pek_value(metric)
        for value, x_pos, css_class, runner in (
            (bare, center - bar_width - gap / 2, "chart-bare", "Bare"),
            (pek, center + gap / 2, "chart-pek", "PEK"),
        ):
            bar_y = y(value)
            baseline_y = y(axis_min)
            tooltip = f"{runner} {metric}: {format_ms(value)} ms"
            parts.append(
                f'<rect class="chart-bar {css_class}" x="{x_pos:.1f}" y="{bar_y:.1f}" '
                f'width="{bar_width:.1f}" height="{baseline_y - bar_y:.1f}"{tooltip_attrs(tooltip)}></rect>'
            )
            parts.append(
                f'<text class="chart-value-label" x="{x_pos + bar_width / 2:.1f}" '
                f'y="{max(top + 18, bar_y - 8):.1f}"{tooltip_attrs(tooltip)}>{format_bar_label(value)}</text>'
            )
        parts.append(f'<text class="chart-x-label" x="{center:.1f}" y="{height - 46}">{html_escape(metric)}</text>')
    parts.append(
        f'<text class="chart-axis-label" x="{left + plot_width / 2:.1f}" '
        f'y="{height - 14}">Metric</text>'
    )
    parts.append(
        f'<text class="chart-axis-label" transform="rotate(-90)" '
        f'x="{-(top + plot_height / 2):.1f}" y="54">Time [ms]</text>'
    )
    parts.append("</svg>")
    return "".join(parts)


def summary_bar_chart_svg(runs: list[dict[str, Any]]) -> str:
    metrics = report_run_metrics(runs)
    deltas = {metric: median_delta(runs, metric) for metric in metrics}
    return bar_chart_svg(
        metrics,
        lambda metric: float(deltas[metric]["bare_ms"]),
        lambda metric: float(deltas[metric]["pek_ms"]),
        "10-run median metric comparison",
    )


def run_bar_chart_svg(run: dict[str, Any]) -> str:
    metrics = report_run_metrics([run])
    return bar_chart_svg(
        metrics,
        lambda metric: metric_value(run, metric, "bare_ms"),
        lambda metric: metric_value(run, metric, "pek_ms"),
        f'{run["name"]} metric bar comparison',
    )


def write_stability_section(runs: list[dict[str, Any]]) -> str:
    return (
        '      <section class="section-card">\n'
        '        <div class="summary-heading"><h2>Run Stability</h2>\n'
        f'{write_stability_metric_controls(runs)}</div>\n'
        '        <div class="benchmark-layout"><div class="chart-panel">\n'
        f'{write_stability_charts(runs)}\n'
        '        </div><div class="table-panel">\n'
        f'{write_stability_table(runs)}\n'
        '        </div></div>\n'
        '      </section>\n'
    )


def write_run_sections(runs: list[dict[str, Any]]) -> str:
    parts = [
        '<section class="section-card run-card">\n',
        '        <div class="run-tabs">\n',
        '          <div class="run-heading"><h2>Runs</h2>\n',
        '            <div class="run-tab-controls">',
    ]
    for index, run in enumerate(runs):
        run_name = html_escape(run["name"])
        tab_id = f"run-tab-{index + 1}"
        panel_id = f"run-tab-panel-{index + 1}"
        checked = " checked" if index == 0 else ""
        active = " is-active" if index == 0 else ""
        parts.append(
            f'<label class="run-tab-label{active}"><input class="run-tab-input" type="radio" '
            f'name="run-tab" id="{tab_id}" aria-controls="{panel_id}" '
            f'data-comparison-href="runs/{run_name}/comparison.json"{checked}>{run_name}</label>'
        )
    parts.append(
        '<a class="title-link run-comparison-link" href="runs/run-01/comparison.json">comparison.json</a>'
        '</div></div><div>'
    )
    for index, run in enumerate(runs):
        panel_id = f"run-tab-panel-{index + 1}"
        active = " is-active" if index == 0 else ""
        parts.extend(
            [
                f'<div class="run-tab-panel{active}" id="{panel_id}">',
                '<div class="benchmark-layout"><div class="chart-panel">',
                run_bar_chart_svg(run),
                '</div><div class="table-panel">',
                write_run_table(run),
                "</div></div></div>\n",
            ]
        )
    parts.append("            </div>\n          </div>\n      </section>\n")
    return "".join(parts)


def write_information_section(comparison: dict[str, Any]) -> str:
    measurement = comparison["measurement"]
    inputs = comparison["inputs"]
    bare_model = inputs.get("bare_model", inputs.get("model", ""))
    pek_opchain = inputs.get("pek_opchain", inputs.get("opchain", ""))
    opchain_ops = describe_opchain_ops(str(pek_opchain))
    opchain_items = "".join(f"<li>{html_escape(op)}</li>" for op in opchain_ops)
    opchain_block = (
        f"<ul>{opchain_items}</ul>" if opchain_items
        else "<p>OpChain operation details were not available in the published artifact.</p>"
    )
    return (
        '<section class="section-card">\n'
        '        <div class="section-heading"><button class="section-toggle" type="button" '
        'aria-expanded="false" aria-controls="section-information"><span>Information</span></button></div>\n'
        '        <div id="section-information" hidden>\n'
        '          <div class="info-grid">\n'
        '            <div><h3>Measurement Cut</h3><p>'
        f'Timed region: <code>{html_escape(measurement["timed_region"])}</code>. '
        'Images are preloaded in memory before the timed loop; one warmup image runs before measurement. '
        'Model/OpChain load, image file I/O, JPEG decode, preload, camera/color adapter cost, and JSONL writing '
        'are outside the per-image timing.</p></div>\n'
        '            <div><h3>Metrics &amp; Stability</h3><ul>'
        '<li><code>avg_ms</code>: average per-image time; lower is faster.</li>'
        '<li><code>p50_ms</code>: median per-image time; half of images were this fast or faster.</li>'
        '<li><code>p75_ms</code>: 75% of images were this fast or faster.</li>'
        '<li><code>p95_ms</code>: tail latency; 95% of images were this fast or faster.</li>'
        '<li><code>p99_ms</code>: extreme tail latency; 99% of images were this fast or faster.</li>'
        '<li>Run Stability shows per-run percentile timings; flatter lines and smaller min-max ranges are steadier.</li>'
        '</ul></div>\n'
        '            <div><h3>Bare Run</h3><p>'
        'Python Ultralytics YOLO predict loop using the same prepared image list and image size. '
        f'Model: <code>{html_escape(bare_model)}</code>.</p></div>\n'
        '            <div><h3>PEK Run</h3><p>'
        'C++ PEK OpChain runner using the same prepared image list and image size. '
        f'OpChain: <code>{html_escape(pek_opchain)}</code>.</p></div>\n'
        f'            <div><h3>OpChain Operations</h3>{opchain_block}</div>\n'
        '          </div>\n'
        '        </div>\n'
        '      </section>\n'
    )


def write_report_page(
    target: Path,
    site_dir: Path,
    title: str,
    meta_html: str,
    runs: list[dict[str, Any]],
) -> None:
    first = runs[0]["comparison"]
    inputs = first["inputs"]
    measurement = first["measurement"]
    back_href = rel_to_report_root(target, site_dir)
    css_href = f"{back_href}report-index.css"
    body = render_template(
        "report.html.in",
        {
            "title": html_escape(title),
            "back_href": html_escape(back_href),
            "meta_html": meta_html,
            "overall_result": overall_result_block(runs),
            "timed_region": html_escape(measurement["timed_region"]),
            "image_count": html_escape(inputs["image_count"]),
            "imgsz": html_escape(inputs["imgsz"]),
            "device": html_escape(inputs["device"]),
            "run_count": len(runs),
            "information_section": write_information_section(first),
            "summary_chart": summary_bar_chart_svg(runs),
            "summary_table": write_summary_table(runs),
            "stability_section": write_stability_section(runs),
            "run_sections": write_run_sections(runs),
            "chart_tooltip": chart_tooltip_html(),
        },
    )
    (target / INDEX_HTML).write_text(render_page(f"{title} - YOLO benchmark", css_href, body), encoding="utf-8")


def report_overall_badge(report_dir: Path) -> str:
    runs = []
    for comparison_path in sorted((report_dir / "runs").glob("run-*/comparison.json")):
        try:
            runs.append({"comparison": load_json(comparison_path)})
        except (OSError, json.JSONDecodeError, PublishError):
            continue
    return overall_result_label(runs) if runs else '<span class="verdict verdict-neutral">n/a</span>'


def report_link(path: str, title: str, meta: str, badge_html: str) -> str:
    return (
        f'<a class="report-link" href="{html_escape(path)}"><span><span class="report-title">{html_escape(title)}</span>'
        f'<span class="report-meta">{html_escape(meta)}</span></span>{badge_html}</a>\n'
    )


def write_yolo_index(site_dir: Path, repository: str) -> None:
    yolo_dir = site_dir / REPORT_ROOT
    yolo_dir.mkdir(parents=True, exist_ok=True)
    nightly = yolo_dir / "nightly"
    if (nightly / INDEX_HTML).is_file():
        meta = read_first_line(nightly / REPORT_INDEX_META, "Scheduled develop run")
        nightly_reports = report_link("nightly/index.html", "Latest nightly", meta, report_overall_badge(nightly))
    else:
        nightly_reports = '          <div class="empty">No nightly report published yet.</div>\n'

    manual_dir = yolo_dir / "manual"
    manual_reports = []
    if manual_dir.is_dir():
        manual_reports = [path for path in manual_dir.iterdir() if (path / INDEX_HTML).is_file()]
    if manual_reports:
        manual_report_links = []
        for report_dir in sorted(manual_reports, key=lambda path: path.name, reverse=True):
            meta = read_first_line(report_dir / REPORT_INDEX_META, "Manual run")
            manual_report_links.append(report_link(
                f"manual/{report_dir.name}/index.html", f"Run {report_dir.name}", meta, report_overall_badge(report_dir)))
        manual_reports_html = "".join(manual_report_links)
    else:
        manual_reports_html = '          <div class="empty">No manual report published yet.</div>\n'

    prs_dir = yolo_dir / "prs"
    pr_reports = []
    if prs_dir.is_dir():
        pr_reports = [path for path in prs_dir.iterdir() if path.is_dir() and path.name.isdigit()]
    if pr_reports:
        pr_report_links = []
        for pr_dir in sorted(pr_reports, key=lambda path: int(path.name)):
            if not (pr_dir / INDEX_HTML).is_file():
                continue
            meta = read_first_line(pr_dir / REPORT_INDEX_META, "Published report")
            title = pr_report_title(pr_dir.name, repository)
            pr_report_links.append(
                report_link(f"prs/{pr_dir.name}/index.html", title, meta, report_overall_badge(pr_dir))
            )
        pr_reports_html = "".join(pr_report_links)
    else:
        pr_reports_html = '          <div class="empty">No PR report published yet.</div>\n'

    body = render_template(
        "index.html.in",
        {
            "product_title": html_escape(PRODUCT_TITLE),
            "nightly_reports": nightly_reports,
            "manual_reports": manual_reports_html,
            "pr_reports": pr_reports_html,
        },
    )
    (yolo_dir / INDEX_HTML).write_text(
        render_page(f"{PRODUCT_TITLE} - YOLO benchmark reports", "report-index.css", body), encoding="utf-8")


def select_target(site_dir: Path, repository: str, event: str, branch: str) -> tuple[Path, str, str]:
    yolo_dir = site_dir / REPORT_ROOT
    if event == "pull_request":
        head_repository = env("UPSTREAM_HEAD_REPOSITORY")
        if head_repository and head_repository != repository:
            raise PublishError(f"Skipping PR YOLO report from untrusted repository: {head_repository}.")
        pr_number = env("UPSTREAM_PR_NUMBER")
        if not pr_number:
            raise PublishError("No PR number found for upstream run; skipping Pages publish.")
        return yolo_dir / "prs" / pr_number, pr_number, f"PR #{pr_number}"
    if event == "schedule":
        if branch != "develop":
            raise PublishError(f"Skipping scheduled YOLO report from {branch}; expected develop.")
        return yolo_dir / "nightly", "", "Latest nightly"
    if event == "workflow_dispatch":
        run_id = require_env("UPSTREAM_RUN_ID")
        return yolo_dir / "manual" / run_id, "", f"Manual run {run_id}"
    raise PublishError(f"Skipping YOLO report from unsupported event: {event}.")


def publish_report(site_dir: Path, storage_branch: str) -> None:
    repository = require_env("GITHUB_REPOSITORY")
    event = require_env("UPSTREAM_EVENT")
    branch = require_env("UPSTREAM_HEAD_BRANCH")
    head_sha = require_env("UPSTREAM_HEAD_SHA")
    conclusion = require_env("UPSTREAM_CONCLUSION")
    run_id = require_env("UPSTREAM_RUN_ID")
    run_attempt = require_env("UPSTREAM_RUN_ATTEMPT")

    if conclusion not in {"success", "failure"}:
        print(f"Skipping YOLO report from {conclusion} upstream run.")
        set_output("deploy", "false")
        return

    try:
        target, pr_number, title = select_target(site_dir, repository, event, branch)
    except PublishError as error:
        print(error)
        set_output("deploy", "false")
        return

    index_meta_text = build_report_index_meta_text(branch, head_sha, run_id, run_attempt)
    meta_html = build_report_meta_html(repository, event, pr_number, branch, head_sha, run_id, run_attempt)

    with tempfile.TemporaryDirectory() as tmpdir:
        artifact_dir = Path(tmpdir)
        if not download_report_artifact(artifact_dir, repository, run_id, run_attempt):
            set_output("deploy", "false")
            return

        artifact_root = find_yolo_artifact(artifact_dir)
        if artifact_root is None:
            print("Artifact did not contain a YOLO benchmark runs directory; skipping Pages publish.")
            set_output("deploy", "false")
            return

        runs = load_report_runs(artifact_root)
        checkout_site_branch(site_dir, storage_branch)
        write_selected_artifacts(artifact_root, target, runs)
        (target / REPORT_INDEX_META).write_text(f"{index_meta_text}\n", encoding="utf-8")
        (target / "report-meta.html").write_text(f"{meta_html}\n", encoding="utf-8")
        (target / "commit.txt").write_text(f"{head_sha}\n", encoding="utf-8")
        write_report_page(target, site_dir, title, meta_html, runs)
        write_index_assets(site_dir)
        write_yolo_index(site_dir, repository)
        (site_dir / ".nojekyll").touch()

        changed = push_site_branch(site_dir, storage_branch)
        set_output("deploy", "true" if changed else "false")


def cleanup_closed_pr_reports(site_dir: Path, storage_branch: str, retention_days: int) -> None:
    repository = require_env("GITHUB_REPOSITORY")
    checkout_site_branch(site_dir, storage_branch)
    cutoff = dt.datetime.now(dt.timezone.utc) - dt.timedelta(days=retention_days)
    changed = False

    prs_dir = site_dir / REPORT_ROOT / "prs"
    if prs_dir.is_dir():
        for pr_dir in prs_dir.iterdir():
            if not pr_dir.is_dir() or not pr_dir.name.isdigit():
                continue
            pr_data = pr_state(repository, pr_dir.name)
            if not pr_data:
                continue
            if should_prune_closed_pr(pr_data.get("state", ""), pr_data.get("closedAt", ""), cutoff):
                shutil.rmtree(pr_dir)
                changed = True

    if changed:
        write_index_assets(site_dir)
        write_yolo_index(site_dir, repository)
        pushed = push_site_branch(site_dir, storage_branch)
        set_output("deploy", "true" if pushed else "false")
    else:
        set_output("deploy", "false")


def main(argv: list[str]) -> int:
    if len(argv) != 2 or argv[1] not in {"publish", "cleanup"}:
        usage()
        return 2

    storage_branch = env("YOLO_PAGES_STORAGE_BRANCH", "playwright-pages")
    site_dir = Path(env("YOLO_PAGES_SITE_DIR", "_yolo_benchmark_pages_site"))
    retention_days = parse_retention_days(env("YOLO_PAGES_RETENTION_DAYS", "10"))

    try:
        if argv[1] == "publish":
            publish_report(site_dir, storage_branch)
        else:
            cleanup_closed_pr_reports(site_dir, storage_branch, retention_days)
    except PublishError as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
