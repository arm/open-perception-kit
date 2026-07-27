#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
from __future__ import annotations

import base64
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
ROOT_REPORT_LINKS = (
    ("yolo-benchmark/index.html", "YOLO video", "End-to-end FPS", "video", "Nightly"),
    ("yolo-imageset-benchmark/index.html", "YOLO image set", "COCO latency", "image", "Nightly"),
    ("playwright/index.html", "Playwright", "Browser smoke", "monitor", "Nightly"),
    ("yolo-performance-datasets/index.html", "Datasets", "Benchmark inputs", "database", "Current"),
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
    index = site_dir / "playwright" / "nightly" / INDEX_HTML
    if not index.is_file():
        return "neutral", "No nightly"
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
        counts = {}
        for name in ("expected", "unexpected", "flaky", "skipped"):
            value = stats.get(name)
            if not isinstance(value, int) or isinstance(value, bool) or value < 0:
                return "neutral", "No status"
            counts[name] = value
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile):
        return "neutral", "No status"

    if counts["unexpected"]:
        return "slow", f'{counts["unexpected"]} failed'
    if counts["flaky"]:
        return "neutral", f'{counts["flaky"]} flaky'
    if not stats["ok"]:
        return "slow", "Failed"
    if counts["expected"]:
        return "fast", f'{counts["expected"]} passed'
    if counts["skipped"]:
        return "neutral", f'{counts["skipped"]} skipped'
    return "neutral", "No tests"


def root_card_badge(site_dir: Path, href: str, dataset_count: int | None) -> tuple[str, str]:
    if href == "yolo-benchmark/index.html":
        return yolo_nightly_badge(site_dir, "yolo-benchmark")
    if href == "yolo-imageset-benchmark/index.html":
        return yolo_nightly_badge(site_dir, "yolo-imageset-benchmark")
    if href == "playwright/index.html":
        return playwright_nightly_badge(site_dir)
    if dataset_count:
        return "fast", f"{dataset_count} input{'s' if dataset_count != 1 else ''}"
    return "neutral", "No inputs"


def root_card_icon(name: str) -> str:
    return (
        '<span class="icon"><svg viewBox="0 0 24 24" aria-hidden="true" focusable="false">'
        f"{ROOT_REPORT_ICONS[name]}</svg></span>"
    )


def write_root_index(site_dir: Path, dataset_count: int | None = None) -> None:
    site_dir.mkdir(parents=True, exist_ok=True)
    cards = []
    for href, title, subtitle, icon, source in ROOT_REPORT_LINKS:
        tone, label = root_card_badge(site_dir, href, dataset_count)
        cards.append(
            f'    <a class="card" href="{html_escape(href)}">{root_card_icon(icon)}'
            f'<span class="card-body"><span class="card-head"><strong>{html_escape(title)}</strong>'
            f'<span class="status"><span class="status-source">{html_escape(source)}</span>'
            f'{verdict_html(tone, label)}</span></span>'
            f'<span class="subtitle">{html_escape(subtitle)}</span></span></a>'
        )
    card_html = "\n".join(cards)
    (site_dir / INDEX_HTML).write_text(f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Arm Perception kit reports</title><style>
*{{box-sizing:border-box}}body{{margin:0;font:16px system-ui,sans-serif;background:#101418;color:#edf4f1}}
main{{max-width:920px;margin:0 auto;padding:48px 24px}}h1{{margin:0 0 8px;font-size:34px}}
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
@media(max-width:640px){{main{{padding:28px 16px}}h1{{font-size:28px}}.grid{{grid-template-columns:1fr}}}}
</style></head><body><main><h1>Arm Perception kit reports</h1><p>Reports and benchmark inputs.</p><div class="grid">
{card_html}
</div></main></body></html>
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
