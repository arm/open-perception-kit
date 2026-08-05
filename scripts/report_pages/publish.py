#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
from __future__ import annotations

import base64
import datetime as dt
import html
import io
import json
import os
import re
import shutil
import subprocess
import zipfile
from pathlib import Path


INDEX_HTML = "index.html"
LEGACY_ROOT_PATHS = (INDEX_HTML, "report-index.css", "report-shell.css", "report-shell.js")
LEGACY_PLAYWRIGHT_REPORT_ROOTS = ("nightly", "prs")
WORKFLOW_STATUS_DIRECTORY = "workflow-status"
WORKFLOW_STATUS_MAX_AGE = dt.timedelta(hours=36)
VALGRIND_STATUS_MAX_AGE = dt.timedelta(days=60)
WORKFLOW_STATUS_REPORTS = (
    ("pek-ci", "PEK CI", "Build, browser and quality", "playwright/index.html"),
    ("python-audit", "Python audit", "Python dependency vulnerabilities", "python-audit/index.html"),
    ("docker-scout", "Docker Scout", "Container image vulnerabilities", "docker-scout/index.html"),
    ("workflow-freshness", "Workflow freshness", "GitHub Actions dependencies", "workflow-freshness/index.html"),
    ("yolo-video", "YOLO video", "End-to-end FPS", "yolo-benchmark/index.html"),
    ("yolo-imageset", "YOLO image set", "COCO latency", "yolo-imageset-benchmark/index.html"),
    ("valgrind", "Valgrind", "Memory regression baseline", "valgrind/index.html"),
)
WORKFLOW_PR_REPORTS = {"python-audit", "docker-scout", "workflow-freshness", "valgrind"}
ROOT_REPORT_LINKS = (
    ("yolo-benchmark/index.html", "YOLO video", "End-to-end FPS", "video", "Nightly"),
    ("yolo-imageset-benchmark/index.html", "YOLO image set", "COCO latency", "image", "Nightly"),
    ("playwright/index.html", "Playwright", "Browser smoke", "monitor", "Nightly"),
    ("yolo-performance-datasets/index.html", "Datasets", "Benchmark inputs", "database", "Current"),
)
ROOT_QUALITY_LINKS = (
    ("python-audit/index.html", "Python audit", "Dependency vulnerabilities", "shield", "Nightly"),
    ("docker-scout/index.html", "Docker Scout", "Container vulnerabilities", "binoculars", "Nightly"),
    ("workflow-freshness/index.html", "Workflow freshness", "GitHub Actions dependencies", "refresh", "Nightly"),
    ("valgrind/index.html", "Valgrind", "Memory regression baseline", "memory", "Nightly"),
)
ROOT_NIGHTLY_LINKS = (
    ("nightly-ci/index.html", "Nightly CI", "Scheduled checks at a glance", "moon", "Overview"),
)
ROOT_REPORT_ICONS = {
    "video": (
        '<path d="m16 13 5.2 3.5a.5.5 0 0 0 .8-.4V7.9a.5.5 0 0 0-.8-.4L16 11"/>'
        '<rect x="2" y="6" width="14" height="12" rx="2"/>'
    ),
    "image": (
        '<rect x="3" y="3" width="18" height="18" rx="2"/><circle cx="9" cy="9" r="2"/>'
        '<path d="m21 15-3.1-3.1a2 2 0 0 0-2.8 0L6 21"/>'
    ),
    "monitor": (
        '<path d="m9 10 2 2 4-4"/><rect x="2" y="3" width="20" height="14" rx="2"/>'
        '<path d="M12 17v4M8 21h8"/>'
    ),
    "database": (
        '<ellipse cx="12" cy="5" rx="9" ry="3"/>'
        '<path d="M3 5v14c0 1.7 4 3 9 3s9-1.3 9-3V5M3 12c0 1.7 4 3 9 3s9-1.3 9-3"/>'
    ),
    "shield": (
        '<path d="M20 13c0 5-3.5 7.5-8 9-4.5-1.5-8-4-8-9V5l8-3 8 3v8Z"/>'
        '<path d="m9 12 2 2 4-4"/>'
    ),
    "binoculars": (
        '<path d="m3 14 2-8h4l1 6m11 2-2-8h-4l-1 6M10 9h4"/>'
        '<circle cx="7" cy="15" r="4"/><circle cx="17" cy="15" r="4"/>'
    ),
    "refresh": (
        '<path d="M20 7h-5V2M4 17h5v5"/>'
        '<path d="M18.4 18A8 8 0 0 1 4 17m1.6-11A8 8 0 0 1 20 7"/>'
    ),
    "moon": (
        '<path d="M21 12.8A9 9 0 1 1 11.2 3 7 7 0 0 0 21 12.8Z"/>'
        '<path d="m15.5 6 .5 1.5 1.5.5-1.5.5-.5 1.5-.5-1.5-1.5-.5 1.5-.5Z"/>'
    ),
    "memory": (
        '<rect x="5" y="5" width="14" height="14" rx="2"/><path d="M9 9h6v6H9z"/>'
        '<path d="M9 2v3m6-3v3M9 19v3m6-3v3M2 9h3m-3 6h3m14-6h3m-3 6h3"/>'
    ),
}
YOLO_OVERALL_BADGE_RE = re.compile(
    r'<div class="report-overall"><span>Overall</span>'
    r'<span class="verdict verdict-(fast|slow|neutral)">([^<]+)</span></div>'
)
PLAYWRIGHT_REPORT_ARCHIVE_RE = re.compile(
    r'<template[^>]+id=["\']playwrightReportBase64["\'][^>]*>'
    r'\s*data:application/zip;base64,([^<]+)</template>',
    re.IGNORECASE,
)


class PublishError(RuntimeError):
    pass


def env(name: str, default: str = "") -> str:
    return os.environ.get(name, default)


def require_env(name: str) -> str:
    value = env(name)
    if not value:
        raise PublishError(f"{name} is required.")
    return value


def set_output(name: str, value: str) -> None:
    output = env("GITHUB_OUTPUT")
    if output:
        with open(output, "a", encoding="utf-8") as handle:
            handle.write(f"{name}={value}\n")


def _run(args: list[str], cwd: Path | None = None, quiet: bool = False,
         check: bool = True) -> subprocess.CompletedProcess:
    stdout = subprocess.DEVNULL if quiet else None
    stderr = subprocess.DEVNULL if quiet else None
    return subprocess.run(args, cwd=cwd, check=check, stdout=stdout, stderr=stderr, text=True)


def run(args: list[str], cwd: Path | None = None, quiet: bool = False) -> subprocess.CompletedProcess:
    return _run(args, cwd, quiet)


def run_maybe(args: list[str], cwd: Path | None = None, quiet: bool = False) -> subprocess.CompletedProcess:
    return _run(args, cwd, quiet, check=False)


def capture(args: list[str], cwd: Path | None = None) -> str:
    result = subprocess.run(args, cwd=cwd, check=True, stdout=subprocess.PIPE, text=True)
    return result.stdout


def html_escape(value: object) -> str:
    return html.escape(str(value), quote=True)


def html_anchor(href: str, text: str) -> str:
    return f'<a href="{html_escape(href)}">{html_escape(text)}</a>'


def git_auth_header() -> str:
    token = require_env("GITHUB_TOKEN")
    return base64.b64encode(f"x-access-token:{token}".encode("utf-8")).decode("ascii")


def git(site_dir: Path, args: list[str], auth_header: str = "", check: bool = True,
        quiet: bool = False) -> subprocess.CompletedProcess:
    command = ["git", "-C", str(site_dir)]
    if auth_header:
        command += ["-c", f"http.https://github.com/.extraheader=AUTHORIZATION: basic {auth_header}"]
    return _run([*command, *args], quiet=quiet, check=check)


def checkout_site_branch(site_dir: Path, storage_branch: str, dry_run_env: str, local_user_name: str) -> None:
    if env(dry_run_env) == "1":
        if site_dir.exists():
            shutil.rmtree(site_dir)
        site_dir.mkdir(parents=True)
        git(site_dir, ["init", "-b", storage_branch], quiet=True)
        git(site_dir, ["config", "user.name", local_user_name])
        git(site_dir, ["config", "user.email", "local@example.invalid"])
        return

    repository = require_env("GITHUB_REPOSITORY")
    auth_header = git_auth_header()
    print(f"::add-mask::{auth_header}")

    if site_dir.exists():
        shutil.rmtree(site_dir)
    site_dir.mkdir(parents=True)

    git(site_dir, ["init"])
    git(site_dir, ["remote", "add", "origin", f"https://github.com/{repository}.git"])
    fetched = git(site_dir, ["fetch", "--depth=1", "origin", storage_branch], auth_header, check=False, quiet=True)
    if fetched.returncode == 0:
        git(site_dir, ["checkout", "-B", storage_branch, "FETCH_HEAD"])
    else:
        git(site_dir, ["checkout", "--orphan", storage_branch])
        git(site_dir, ["rm", "-rf", "."], check=False, quiet=True)

    git(site_dir, ["config", "user.name", "github-actions[bot]"])
    git(site_dir, ["config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com"])


def push_site_branch(site_dir: Path, storage_branch: str, dry_run_env: str,
                     commit_message: str, dry_run_label: str) -> bool:
    git(site_dir, ["add", "-A", "."])
    diff = git(site_dir, ["diff", "--cached", "--quiet"], check=False)
    if diff.returncode == 0:
        return False

    git(site_dir, ["commit", "-m", commit_message])
    if env(dry_run_env) == "1":
        print(f"Dry-run: generated {dry_run_label} site at {site_dir}")
        return True

    auth_header = git_auth_header()
    git(site_dir, ["push", "origin", f"HEAD:{storage_branch}"], auth_header)
    return True


def verdict_html(tone: str, label: str) -> str:
    if tone not in {"fast", "slow", "neutral"}:
        raise ValueError(f"Unsupported verdict tone: {tone}")
    return f'<span class="verdict verdict-{tone}">{html_escape(label)}</span>'


def yolo_nightly_badge(site_dir: Path, report_root: str) -> tuple[str, str]:
    index = site_dir / report_root / "nightly" / INDEX_HTML
    if not index.is_file():
        return "neutral", "No nightly"
    try:
        match = YOLO_OVERALL_BADGE_RE.search(index.read_text(encoding="utf-8"))
    except OSError:
        match = None
    if match is None:
        return "neutral", "No status"
    return match.group(1), html.unescape(match.group(2))


def playwright_nightly_badge(site_dir: Path) -> tuple[str, str]:
    indexes = [
        site_dir / "playwright" / directory / INDEX_HTML
        for directory in ("nightly", "nightly-macos")
    ]
    indexes = [index for index in indexes if index.is_file()]
    if not indexes:
        return "neutral", "No nightly"
    counts = {name: 0 for name in ("expected", "unexpected", "flaky", "skipped")}
    all_ok = True
    for index in indexes:
        try:
            content = index.read_text(encoding="utf-8")
            match = PLAYWRIGHT_REPORT_ARCHIVE_RE.search(content)
            if match is None:
                return "neutral", "No status"
            payload = re.sub(r"\s+", "", match.group(1))
            with zipfile.ZipFile(io.BytesIO(base64.b64decode(payload, validate=True))) as archive:
                stats = json.loads(archive.read("report.json")).get("stats")
            if not isinstance(stats, dict) or not isinstance(stats.get("ok"), bool):
                return "neutral", "No status"
            all_ok = all_ok and stats["ok"]
            for name in counts:
                value = stats.get(name)
                if not isinstance(value, int) or isinstance(value, bool) or value < 0:
                    return "neutral", "No status"
                counts[name] += value
        except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile):
            return "neutral", "No status"

    if counts["unexpected"]:
        return "slow", f'{counts["unexpected"]} failed'
    if counts["flaky"]:
        return "neutral", f'{counts["flaky"]} flaky'
    if not all_ok:
        return "slow", "Failed"
    if counts["expected"]:
        return "fast", f'{counts["expected"]} passed'
    if counts["skipped"]:
        return "neutral", f'{counts["skipped"]} skipped'
    return "neutral", "No tests"


def root_card_badge(site_dir: Path, href: str, dataset_count: int | None,
                    now: dt.datetime | None = None) -> tuple[str, str]:
    now = now or dt.datetime.now(dt.timezone.utc)
    if href == "yolo-benchmark/index.html":
        return yolo_nightly_badge(site_dir, "yolo-benchmark")
    if href == "yolo-imageset-benchmark/index.html":
        return yolo_nightly_badge(site_dir, "yolo-imageset-benchmark")
    if href == "playwright/index.html":
        return playwright_nightly_badge(site_dir)
    source = href.removesuffix("/index.html")
    if source in WORKFLOW_PR_REPORTS:
        return workflow_status_badge(read_workflow_status(site_dir, source), now)
    if href == "nightly-ci/index.html":
        return nightly_status_verdict(site_dir, now)
    if dataset_count:
        return "fast", f"{dataset_count} input{'s' if dataset_count != 1 else ''}"
    return "neutral", "No inputs"


def root_card_icon(name: str) -> str:
    return (
        '<span class="icon"><svg viewBox="0 0 24 24" aria-hidden="true" focusable="false">'
        f"{ROOT_REPORT_ICONS[name]}</svg></span>"
    )


def read_workflow_status(site_dir: Path, source: str,
                         relative_path: str = "nightly.json") -> dict[str, object] | None:
    path = site_dir / WORKFLOW_STATUS_DIRECTORY / source / relative_path
    if relative_path == "nightly.json" and not path.exists():
        path = site_dir / WORKFLOW_STATUS_DIRECTORY / f"{source}.json"
    try:
        status = json.loads(path.read_text(encoding="utf-8"))
        required = ("conclusion", "head_sha", "repository", "run_id", "updated_at")
        if not isinstance(status, dict) or any(not isinstance(status.get(name), str) for name in required):
            return None
        if not status["run_id"].isdigit() or not re.fullmatch(r"[0-9a-f]{40}", status["head_sha"]):
            return None
        if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", status["repository"]):
            return None
        updated_at = dt.datetime.fromisoformat(status["updated_at"].replace("Z", "+00:00"))
        if updated_at.tzinfo is None:
            return None
        status["updated_at"] = updated_at.astimezone(dt.timezone.utc).isoformat()
        return status
    except (OSError, TypeError, ValueError, json.JSONDecodeError):
        return None


def workflow_status_verdict(status: dict[str, object] | None,
                            now: dt.datetime) -> tuple[str, str]:
    if status is None:
        return "neutral", "Unavailable"
    updated_at = dt.datetime.fromisoformat(str(status["updated_at"]).replace("Z", "+00:00"))
    max_age = (
        VALGRIND_STATUS_MAX_AGE
        if status.get("event") == "push" and status.get("workflow") == "Valgrind Baseline Artifact"
        else WORKFLOW_STATUS_MAX_AGE
    )
    if now.astimezone(dt.timezone.utc) - updated_at > max_age:
        return "neutral", "Stale"
    conclusion = str(status["conclusion"])
    if conclusion == "success":
        return "fast", "Passed"
    if conclusion in {"failure", "timed_out", "action_required", "startup_failure"}:
        return "slow", conclusion.replace("_", " ").title()
    if conclusion in {"cancelled", "skipped"}:
        return "neutral", conclusion.title()
    return "neutral", "Partial"


def workflow_status_badge(status: dict[str, object] | None,
                          now: dt.datetime) -> tuple[str, str]:
    tone, label = workflow_status_verdict(status, now)
    metric = status.get("metric") if status is not None else None
    if isinstance(metric, str) and metric and label != "Stale":
        metric_tone = status.get("metric_tone")
        if metric_tone in {"fast", "slow", "neutral"}:
            tone = str(metric_tone)
        label = metric
    return tone, label


def nightly_status_verdict(site_dir: Path, now: dt.datetime) -> tuple[str, str]:
    tones = [
        workflow_status_verdict(read_workflow_status(site_dir, source), now)[0]
        for source, _title, _subtitle, _href in WORKFLOW_STATUS_REPORTS
        if source != "valgrind"
    ]
    attention = sum(tone != "fast" for tone in tones)
    if not attention:
        return "fast", "All passed"
    label = f"{attention} need{'s' if attention == 1 else ''} attention"
    return ("slow" if "slow" in tones else "neutral"), label


def status_meta(status: dict[str, object]) -> str:
    updated_at = dt.datetime.fromisoformat(str(status["updated_at"]).replace("Z", "+00:00"))
    branch = status.get("head_branch") or ("develop" if status.get("event") == "schedule" else "")
    attempt = status.get("run_attempt", "1")
    return (
        f'{branch} @ {str(status["head_sha"])[:12]} | run {status["run_id"]} '
        f'attempt {attempt} | {updated_at.strftime("%b %d, %Y %H:%M UTC")}'
    )


def status_summary(status: dict[str, object]) -> str:
    messages = status.get("summary")
    if not isinstance(messages, list):
        return ""
    return "".join(
        f'<span class="report-meta">{html_escape(message[:240])}</span>'
        for message in messages[:3]
        if isinstance(message, str) and message
    )


def status_link(status: dict[str, object] | None, title: str, now: dt.datetime,
                href: str | None = None) -> str:
    if status is None:
        return (
            '<div class="report-link"><span>'
            f'<span class="report-title"><a href="{html_escape(href or "#")}">'
            f'{html_escape(title)}</a></span><span class="report-meta">No status published yet.</span>'
            f'</span>{verdict_html("neutral", "Unavailable")}</div>'
        )
    tone, label = workflow_status_badge(status, now)
    run_url = f'https://github.com/{status["repository"]}/actions/runs/{status["run_id"]}'
    run_label = "Job summary" if status.get("workflow") == "Valgrind Baseline Artifact" else "Run"
    return (
        '<div class="report-link"><span>'
        f'<span class="report-title"><a href="{html_escape(href or run_url)}">'
        f'{html_escape(title)}</a></span><span class="report-meta">'
        f'{html_escape(status_meta(status))} · <a href="{html_escape(run_url)}">'
        f'{run_label}</a></span>'
        f'{status_summary(status)}</span>{verdict_html(tone, label)}</div>'
    )


def report_index_page(title: str, eyebrow: str, primary_title: str,
                      primary: str, prs: str = "") -> str:
    return f'''<!doctype html>
<html lang="en" style="scrollbar-gutter: stable both-edges;"><head><meta charset="utf-8">
<meta name="color-scheme" content="dark light"><meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>{html_escape(title)}</title><link rel="stylesheet" href="report-index.css"></head><body><main>
<header><div class="eyebrow">{html_escape(eyebrow)}</div><h1>Arm Perception kit</h1></header>
<section><h2>{html_escape(primary_title)}</h2><div class="report-list">{primary}</div></section>
{prs}<a class="back-link" href="../index.html">Back to all reports</a>
</main></body></html>'''


def write_status_indexes(site_dir: Path, now: dt.datetime) -> None:
    css_source = Path(__file__).resolve().parents[1] / "playwright/pages/assets/report-index.css"
    status_root = site_dir / WORKFLOW_STATUS_DIRECTORY
    for source, title, _subtitle, _href in WORKFLOW_STATUS_REPORTS:
        if source not in WORKFLOW_PR_REPORTS:
            continue
        target = site_dir / source
        target.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(css_source, target / "report-index.css")
        primary_title = "Nightly"
        primary_status = read_workflow_status(site_dir, source)
        primary = (
            status_link(primary_status, f"Latest {primary_title.lower()}", now)
            if primary_status else f'<div class="empty">No {primary_title.lower()} report published yet.</div>'
        )
        pr_links = []
        for path in sorted((status_root / source / "prs").glob("*.json"), reverse=True):
            status = read_workflow_status(site_dir, source, f"prs/{path.name}")
            if status is not None:
                pr_links.append(status_link(status, f"PR #{path.stem}", now))
        prs = (
            '<section><h2>Pull Requests</h2><div class="report-list">'
            + ("".join(pr_links) if pr_links else '<div class="empty">No PR report published yet.</div>')
            + "</div></section>"
        )
        (target / INDEX_HTML).write_text(
            report_index_page(f"{title} reports", f"{title} reports", primary_title, primary, prs),
            encoding="utf-8",
        )

    target = site_dir / "nightly-ci"
    target.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(css_source, target / "report-index.css")
    links = []
    for source, title, _subtitle, href in WORKFLOW_STATUS_REPORTS:
        if source == "valgrind":
            continue
        status = read_workflow_status(site_dir, source)
        links.append(status_link(status, title, now, f"../{href}"))
    nightly = "".join(links) if links else '<div class="empty">No nightly results published yet.</div>'
    (target / INDEX_HTML).write_text(
        report_index_page("Nightly CI report", "Nightly CI report", "Nightly", nightly),
        encoding="utf-8",
    )


def write_root_index(site_dir: Path, dataset_count: int | None = None,
                     now: dt.datetime | None = None) -> None:
    site_dir.mkdir(parents=True, exist_ok=True)
    now = now or dt.datetime.now(dt.timezone.utc)
    write_status_indexes(site_dir, now)
    groups = []
    for links in (ROOT_REPORT_LINKS, ROOT_QUALITY_LINKS, ROOT_NIGHTLY_LINKS):
        cards = []
        for href, title, subtitle, icon, source in links:
            tone, label = root_card_badge(site_dir, href, dataset_count, now)
            cards.append(
                f'    <a class="card" href="{html_escape(href)}">{root_card_icon(icon)}'
                f'<span class="card-body"><span class="card-head"><strong>{html_escape(title)}</strong>'
                f'<span class="status"><span class="status-source">{html_escape(source)}</span>'
                f'{verdict_html(tone, label)}</span></span>'
                f'<span class="subtitle">{html_escape(subtitle)}</span></span></a>'
            )
        groups.append("\n".join(cards))
    report_html, quality_html, nightly_html = groups
    (site_dir / INDEX_HTML).write_text(f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Arm Perception kit reports</title><style>
*{{box-sizing:border-box}}body{{margin:0;font:16px system-ui,sans-serif;background:#101418;color:#edf4f1}}
main{{max-width:920px;margin:0 auto;padding:48px 24px}}h1{{margin:0 0 8px;font-size:34px}}h2{{margin:30px 0 10px;font-size:20px}}
p{{margin:0 0 24px;color:#b8c7c1}}.grid{{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}}
.card{{display:grid;grid-template-columns:30px minmax(0,1fr);align-items:center;gap:12px;min-width:0;padding:12px 14px;border:1px solid #2f4a43;border-radius:8px;color:inherit;text-decoration:none;background:#17211f}}
.card:hover{{border-color:#49b27d}}.card:focus-visible{{outline:2px solid #49b27d;outline-offset:2px}}
.icon{{display:grid;width:30px;height:30px;place-items:center;border:1px solid #2f4a43;border-radius:6px;color:#49b27d}}
.icon svg{{width:17px;height:17px;fill:none;stroke:currentColor;stroke-linecap:round;stroke-linejoin:round;stroke-width:2}}
.card-body,.card-head{{min-width:0}}.card-head{{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:6px 10px}}
.status{{display:flex;flex-wrap:wrap;align-items:center;gap:4px 6px;min-width:0}}.status-source{{color:#9fb0aa;font-size:10px;font-weight:700;line-height:12px;text-transform:uppercase}}
.verdict{{max-width:100%;border-radius:999px;padding:2px 7px;font-size:11px;font-weight:700;line-height:15px;white-space:nowrap}}
.verdict-fast{{background:#13271b;color:#3fb950}}.verdict-slow{{background:#331c1f;color:#ff7b72}}.verdict-neutral{{background:#26302d;color:#9fb0aa}}
strong{{display:block;flex:1 1 110px;min-width:0;font-size:17px;line-height:22px}}.subtitle{{display:block;margin-top:2px;color:#9fb0aa;font-size:13px;line-height:18px}}
.nightly-status{{margin-top:32px;padding-top:18px;border-top:1px solid #2f4a43}}
@media(max-width:640px){{main{{padding:28px 16px}}h1{{font-size:28px}}.grid{{grid-template-columns:1fr}}}}
</style></head><body><main><h1>Arm Perception kit reports</h1><p>Reports and benchmark inputs.</p><h2>Reports</h2><div class="grid">
{report_html}
</div><section class="nightly-status"><div class="grid">
{quality_html}
</div></section><section class="nightly-status"><div class="grid">
{nightly_html}
</div></section></main></body></html>
""", encoding="utf-8")


def remove_legacy_root_site(site_dir: Path) -> bool:
    changed = False
    for name in LEGACY_PLAYWRIGHT_REPORT_ROOTS:
        source = site_dir / name
        target = site_dir / "playwright" / name
        if source.exists() and not target.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(source), str(target))
            changed = True
    for name in LEGACY_ROOT_PATHS:
        path = site_dir / name
        if not path.exists():
            continue
        if path.is_dir():
            shutil.rmtree(path)
        else:
            path.unlink()
        changed = True
    return changed
