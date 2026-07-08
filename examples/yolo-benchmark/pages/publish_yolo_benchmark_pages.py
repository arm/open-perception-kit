#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
# Updates the persisted YOLO benchmark report site used by GitHub Pages.
################################################################

from __future__ import annotations

import datetime as dt
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[2]
PLAYWRIGHT_PUBLISHER = REPO_ROOT / "scripts" / "playwright" / "pages" / "publish_playwright_pages.py"
REPORT_ROOT = "yolo-benchmark"
PRODUCT_TITLE = "Arm Perception kit"
METRICS = ("avg_ms", "p50_ms", "p95_ms", "p99_ms")


def import_playwright_pages():
    spec = importlib.util.spec_from_file_location("publish_playwright_pages", PLAYWRIGHT_PUBLISHER)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


# Reuse the existing Pages publisher primitives so YOLO and Playwright reports
# keep the same storage-branch behavior and visual shell.
pages = import_playwright_pages()
PublishError = pages.PublishError


def mirror_playwright_env() -> None:
    os.environ.setdefault("REPORT_PAGES_LABEL", "YOLO benchmark Pages")
    os.environ.setdefault("REPORT_PAGES_COMMIT_MESSAGE", "Update YOLO benchmark report pages")
    for yolo_name, playwright_name in (
        ("YOLO_PAGES_DRY_RUN", "PLAYWRIGHT_PAGES_DRY_RUN"),
        ("YOLO_PAGES_SITE_DIR", "PLAYWRIGHT_PAGES_SITE_DIR"),
        ("YOLO_PAGES_STORAGE_BRANCH", "PLAYWRIGHT_PAGES_STORAGE_BRANCH"),
    ):
        if os.environ.get(yolo_name) and not os.environ.get(playwright_name):
            os.environ[playwright_name] = os.environ[yolo_name]


def usage() -> None:
    print("Usage: examples/yolo-benchmark/pages/publish.sh publish|cleanup", file=sys.stderr)


def env(name: str, default: str = "") -> str:
    return pages.env(name, default)


def require_env(name: str) -> str:
    return pages.require_env(name)


def html_escape(value: object) -> str:
    return pages.html_escape(str(value))


def html_anchor(href: str, text: str) -> str:
    return pages.html_anchor(href, text)


def rel_to_site_root(target: Path, site_dir: Path) -> str:
    depth = len(target.relative_to(site_dir).parts)
    return "../" * depth


def build_source_meta_html(repository: str, branch: str, head_sha: str, run_id: str, run_attempt: str) -> str:
    return pages.build_source_meta_html(repository, branch, head_sha, run_id, run_attempt)


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
    return pages.build_report_index_meta_text(branch, head_sha, run_id, run_attempt, now=now)


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
        shutil.copytree(source, artifact_dir / "local-artifact" / REPORT_ROOT, ignore=local_artifact_ignore(source))
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
    for path in artifact_dir.rglob(REPORT_ROOT):
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


def format_ms(value: float) -> str:
    return f"{value:.3f}"


def format_ratio(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.3f}x"


def format_percent(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.1f}%"


def run_delta(run: dict[str, Any], metric: str) -> dict[str, Any]:
    return run["comparison"]["timing_delta"]["per_image_ms"][metric]


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


def chart_svg(runs: list[dict[str, Any]], metric: str) -> str:
    width = 960
    height = 240
    left = 58
    right = 20
    top = 20
    bottom = 44
    plot_width = width - left - right
    plot_height = height - top - bottom
    max_value = max(metric_value(run, metric, key) for run in runs for key in ("bare_ms", "pek_ms"))
    max_value = max(max_value, 1.0)
    step_count = 4

    def x(index: int) -> float:
        if len(runs) == 1:
            return left + plot_width / 2
        return left + (plot_width * index / (len(runs) - 1))

    def y(value: float) -> float:
        return top + plot_height - (value / max_value * plot_height)

    bare_points = " ".join(
        f"{x(index):.1f},{y(metric_value(run, metric, 'bare_ms')):.1f}"
        for index, run in enumerate(runs)
    )
    pek_points = " ".join(
        f"{x(index):.1f},{y(metric_value(run, metric, 'pek_ms')):.1f}"
        for index, run in enumerate(runs)
    )
    parts = [
        f'<svg class="metric-chart" viewBox="0 0 {width} {height}" role="img" '
        f'aria-label="{html_escape(metric)} benchmark chart">',
        '<g class="chart-grid">',
    ]
    for step in range(step_count + 1):
        value = max_value * step / step_count
        grid_y = y(value)
        parts.append(f'<line x1="{left}" y1="{grid_y:.1f}" x2="{width - right}" y2="{grid_y:.1f}"></line>')
        parts.append(f'<text x="8" y="{grid_y + 4:.1f}">{format_ms(value)}</text>')
    parts.append("</g>")
    parts.append(f'<polyline class="chart-line chart-bare" points="{bare_points}"></polyline>')
    parts.append(f'<polyline class="chart-line chart-pek" points="{pek_points}"></polyline>')
    for index, run in enumerate(runs):
        run_x = x(index)
        bare_y = y(metric_value(run, metric, "bare_ms"))
        pek_y = y(metric_value(run, metric, "pek_ms"))
        delta = run_delta(run, metric)
        label = format_percent(delta.get("delta_percent"))
        parts.append(
            f'<line class="chart-delta" x1="{run_x:.1f}" y1="{bare_y:.1f}" '
            f'x2="{run_x:.1f}" y2="{pek_y:.1f}"></line>'
        )
        parts.append(f'<circle class="chart-point chart-bare" cx="{run_x:.1f}" cy="{bare_y:.1f}" r="4"></circle>')
        parts.append(f'<circle class="chart-point chart-pek" cx="{run_x:.1f}" cy="{pek_y:.1f}" r="4"></circle>')
        parts.append(
            f'<text class="chart-delta-label" x="{run_x:.1f}" '
            f'y="{min(bare_y, pek_y) - 7:.1f}">{html_escape(label)}</text>'
        )
        parts.append(
            f'<text class="chart-run-label" x="{run_x:.1f}" '
            f'y="{height - 14}">{html_escape(run["name"].replace("run-", ""))}</text>'
        )
    parts.append("</svg>")
    return "".join(parts)


def write_metric_table(runs: list[dict[str, Any]]) -> str:
    parts = [
        '<div class="table-scroll"><table class="benchmark-table">',
        "<thead><tr><th>run</th><th>metric</th><th>bare ms</th><th>PEK ms</th><th>delta ms</th><th>ratio</th><th>delta %</th></tr></thead><tbody>",
    ]
    for run in runs:
        for metric in METRICS:
            delta = run_delta(run, metric)
            parts.append(
                "<tr>"
                f"<td>{html_escape(run['name'])}</td>"
                f"<td>{html_escape(metric)}</td>"
                f"<td>{format_ms(float(delta['bare_ms']))}</td>"
                f"<td>{format_ms(float(delta['pek_ms']))}</td>"
                f"<td>{format_ms(float(delta['delta_ms']))}</td>"
                f"<td>{format_ratio(delta.get('ratio'))}</td>"
                f"<td>{format_percent(delta.get('delta_percent'))}</td>"
                "</tr>"
            )
    parts.append("</tbody></table></div>")
    return "".join(parts)


def write_raw_links(runs: list[dict[str, Any]]) -> str:
    parts = ['<div class="report-list">']
    for run in runs:
        run_name = html_escape(run["name"])
        href = f"runs/{run_name}/comparison.json"
        parts.append(
            f'<a class="report-link" href="{href}"><span><span class="report-title">{run_name}</span>'
            '<span class="report-meta">comparison.json, comparison.md, bare and PEK summaries</span></span>'
            '<span class="badge">JSON</span></a>'
        )
    parts.append("</div>")
    return "".join(parts)


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
    css_href = f"{rel_to_site_root(target, site_dir)}report-index.css"
    back_href = "../" * len(target.relative_to(site_dir / REPORT_ROOT).parts)
    parts = [
        pages.write_index_head(f"{title} - YOLO benchmark", css_href),
        """      <header>
        <div class="eyebrow">YOLO benchmark</div>
        <h1>""",
        html_escape(title),
        """</h1>
        <p>""",
        meta_html,
        """</p>
      </header>
      <section>
        <h2>Benchmark Definition</h2>
        <div class="report-list">
""",
        f'          <div class="empty"><span><span class="report-title">{html_escape(measurement["timed_region"])}</span>'
        f'<span class="report-meta">{html_escape(inputs["image_count"])} images | imgsz {html_escape(inputs["imgsz"])} | '
        f'{html_escape(inputs["device"])}</span></span><span class="badge">{len(runs)} runs</span></div>\n',
        """        </div>
      </section>
      <section>
        <h2>Performance Plots</h2>
        <div class="chart-legend"><span class="legend-bare">bare</span><span class="legend-pek">PEK</span></div>
""",
    ]
    for metric in METRICS:
        parts.extend([f'        <h3>{html_escape(metric)}</h3>\n', chart_svg(runs, metric), "\n"])
    parts.extend(
        [
            """      </section>
      <section>
        <h2>Per-run Values</h2>
""",
            write_metric_table(runs),
            """      </section>
      <section>
        <h2>Raw Artifacts</h2>
""",
            write_raw_links(runs),
            f'        <a class="back-link" href="{html_escape(back_href)}index.html">Back to YOLO report index</a>\n',
            "      </section>\n",
            pages.write_index_footer(),
        ]
    )
    (target / pages.INDEX_HTML).write_text("".join(parts), encoding="utf-8")


def report_link(path: str, title: str, meta: str, badge: str = "Open") -> str:
    return (
        f'<a class="report-link" href="{html_escape(path)}"><span><span class="report-title">{html_escape(title)}</span>'
        f'<span class="report-meta">{html_escape(meta)}</span></span><span class="badge">{html_escape(badge)}</span></a>\n'
    )


def write_yolo_index(site_dir: Path, repository: str) -> None:
    yolo_dir = site_dir / REPORT_ROOT
    yolo_dir.mkdir(parents=True, exist_ok=True)
    parts = [
        pages.write_index_head(f"{PRODUCT_TITLE} - YOLO benchmark reports", "../report-index.css"),
        """      <header>
        <div class="eyebrow">YOLO benchmark reports</div>
        <h1>Arm Perception kit</h1>
      </header>
      <section>
        <h2>Nightly</h2>
        <div class="report-list">
""",
    ]
    nightly = yolo_dir / "nightly"
    if (nightly / pages.INDEX_HTML).is_file():
        meta = pages.read_first_line(nightly / pages.REPORT_INDEX_META, "Scheduled develop run")
        parts.append(report_link("nightly/index.html", "Latest nightly", meta))
    else:
        parts.append('          <div class="empty">No nightly report published yet.</div>\n')

    parts.append(
        """        </div>
      </section>
      <section>
        <h2>Manual</h2>
        <div class="report-list">
"""
    )
    manual_dir = yolo_dir / "manual"
    manual_reports = []
    if manual_dir.is_dir():
        manual_reports = [path for path in manual_dir.iterdir() if (path / pages.INDEX_HTML).is_file()]
    if manual_reports:
        for report_dir in sorted(manual_reports, key=lambda path: path.name, reverse=True):
            meta = pages.read_first_line(report_dir / pages.REPORT_INDEX_META, "Manual run")
            parts.append(report_link(f"manual/{report_dir.name}/index.html", f"Run {report_dir.name}", meta))
    else:
        parts.append('          <div class="empty">No manual report published yet.</div>\n')

    parts.append(
        """        </div>
      </section>
      <section>
        <h2>Pull Requests</h2>
        <div class="report-list">
"""
    )
    prs_dir = yolo_dir / "prs"
    pr_reports = []
    if prs_dir.is_dir():
        pr_reports = [path for path in prs_dir.iterdir() if path.is_dir() and path.name.isdigit()]
    if pr_reports:
        for pr_dir in sorted(pr_reports, key=lambda path: int(path.name)):
            if not (pr_dir / pages.INDEX_HTML).is_file():
                continue
            meta = pages.read_first_line(pr_dir / pages.REPORT_INDEX_META, "Published report")
            title = pages.pr_report_title(pr_dir.name, repository)
            parts.append(report_link(f"prs/{pr_dir.name}/index.html", title, meta))
    else:
        parts.append('          <div class="empty">No PR report published yet.</div>\n')

    parts.extend(["""        </div>
      </section>
""", pages.write_index_footer()])
    (yolo_dir / pages.INDEX_HTML).write_text("".join(parts), encoding="utf-8")


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
        pages.set_output("deploy", "false")
        return

    try:
        target, pr_number, title = select_target(site_dir, repository, event, branch)
    except PublishError as error:
        print(error)
        pages.set_output("deploy", "false")
        return

    index_meta_text = build_report_index_meta_text(branch, head_sha, run_id, run_attempt)
    meta_html = build_report_meta_html(repository, event, pr_number, branch, head_sha, run_id, run_attempt)

    with tempfile.TemporaryDirectory() as tmpdir:
        artifact_dir = Path(tmpdir)
        if not download_report_artifact(artifact_dir, repository, run_id, run_attempt):
            pages.set_output("deploy", "false")
            return

        artifact_root = find_yolo_artifact(artifact_dir)
        if artifact_root is None:
            print("Artifact did not contain a YOLO benchmark runs directory; skipping Pages publish.")
            pages.set_output("deploy", "false")
            return

        runs = load_report_runs(artifact_root)
        pages.checkout_site_branch(site_dir, storage_branch)
        write_selected_artifacts(artifact_root, target, runs)
        (target / pages.REPORT_INDEX_META).write_text(f"{index_meta_text}\n", encoding="utf-8")
        (target / "report-meta.html").write_text(f"{meta_html}\n", encoding="utf-8")
        (target / "commit.txt").write_text(f"{head_sha}\n", encoding="utf-8")
        write_report_page(target, site_dir, title, meta_html, runs)
        pages.write_index_assets(site_dir)
        write_yolo_index(site_dir, repository)
        (site_dir / ".nojekyll").touch()

        changed = pages.push_site_branch(site_dir, storage_branch)
        pages.set_output("deploy", "true" if changed else "false")


def cleanup_closed_pr_reports(site_dir: Path, storage_branch: str, retention_days: int) -> None:
    repository = require_env("GITHUB_REPOSITORY")
    pages.checkout_site_branch(site_dir, storage_branch)
    cutoff = dt.datetime.now(dt.timezone.utc) - dt.timedelta(days=retention_days)
    changed = False

    prs_dir = site_dir / REPORT_ROOT / "prs"
    if prs_dir.is_dir():
        for pr_dir in prs_dir.iterdir():
            if not pr_dir.is_dir() or not pr_dir.name.isdigit():
                continue
            pr_data = pages.pr_state(repository, pr_dir.name)
            if not pr_data:
                continue
            if pages.should_prune_closed_pr(pr_data.get("state", ""), pr_data.get("closedAt", ""), cutoff):
                shutil.rmtree(pr_dir)
                changed = True

    if changed:
        pages.write_index_assets(site_dir)
        write_yolo_index(site_dir, repository)
        pushed = pages.push_site_branch(site_dir, storage_branch)
        pages.set_output("deploy", "true" if pushed else "false")
    else:
        pages.set_output("deploy", "false")


def main(argv: list[str]) -> int:
    if len(argv) != 2 or argv[1] not in {"publish", "cleanup"}:
        usage()
        return 2

    mirror_playwright_env()
    storage_branch = env("YOLO_PAGES_STORAGE_BRANCH", env("PLAYWRIGHT_PAGES_STORAGE_BRANCH", "playwright-pages"))
    site_dir = Path(env("YOLO_PAGES_SITE_DIR", env("PLAYWRIGHT_PAGES_SITE_DIR", "_playwright_pages_site")))
    retention_days = pages.parse_retention_days(env("YOLO_PAGES_RETENTION_DAYS", "10"))

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
