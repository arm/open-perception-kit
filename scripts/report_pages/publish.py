#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
from __future__ import annotations

import base64
import html
import os
import shutil
import subprocess
from pathlib import Path


INDEX_HTML = "index.html"
LEGACY_ROOT_PATHS = (INDEX_HTML, "report-index.css", "report-shell.css", "report-shell.js")
LEGACY_PLAYWRIGHT_REPORT_ROOTS = ("nightly", "prs")
ROOT_REPORT_LINKS = (
    ("yolo-benchmark/index.html", "YOLO video", "End-to-end FPS"),
    ("yolo-imageset-benchmark/index.html", "YOLO image set", "COCO latency"),
    ("playwright/index.html", "Playwright", "Browser smoke"),
    ("yolo-performance-datasets/index.html", "Datasets", "Benchmark inputs"),
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


def write_root_index(site_dir: Path) -> None:
    site_dir.mkdir(parents=True, exist_ok=True)
    cards = "\n".join(
        f'    <a href="{html_escape(href)}"><strong>{html_escape(title)}</strong>'
        f'<span>{html_escape(subtitle)}</span></a>'
        for href, title, subtitle in ROOT_REPORT_LINKS
    )
    (site_dir / INDEX_HTML).write_text(f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Arm Perception kit reports</title><style>
body{{margin:0;font:16px system-ui,sans-serif;background:#101418;color:#edf4f1}}main{{max-width:920px;margin:0 auto;padding:48px 24px}}
h1{{margin:0 0 8px;font-size:34px}}p{{margin:0 0 24px;color:#b8c7c1}}.grid{{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}}
a{{display:block;min-width:0;padding:18px;border:1px solid #2f4a43;border-radius:8px;color:inherit;text-decoration:none;background:#17211f}}a:hover{{border-color:#49b27d}}
strong{{display:block;margin-bottom:6px;font-size:19px}}span{{color:#9fb0aa}}
@media(max-width:640px){{main{{padding:28px 16px}}h1{{font-size:28px}}.grid{{grid-template-columns:1fr}}}}
</style></head><body><main><h1>Arm Perception kit reports</h1><p>Reports and benchmark inputs.</p><div class="grid">
{cards}
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
