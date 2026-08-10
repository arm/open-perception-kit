#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
# Updates the persisted Playwright report site used by GitHub Pages.
################################################################

import datetime as dt
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from urllib.parse import quote

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[2]
sys.path.insert(0, str(REPO_ROOT))

from scripts.report_pages.publish import (  # noqa: E402
    PublishError,
    capture,
    checkout_site_branch as common_checkout_site_branch,
    env,
    html_anchor,
    html_escape,
    remove_legacy_root_site,
    require_env,
    retry_storage_branch_update,
    run,
    run_maybe,
    set_output,
    push_site_branch as common_push_site_branch,
    write_root_index,
)


PRODUCT_TITLE = "Arm Perception kit"
REPORT_ROOT = "playwright"
INDEX_HTML = "index.html"
REPORT_INDEX_META = "report-index-meta.txt"
VIDEO_ARTIFACT_META = "video-artifact.json"
ARTIFACT_PREFIX_META = "artifact-prefix.txt"
DEFAULT_ARTIFACT_PREFIX = "rpi-browser-smoke"
MAX_REPORT_BYTES = 500 * 1024 * 1024
PRUNED_REPORT_DATA_SUFFIXES = {".webm", ".zip"}
DRY_RUN_ENV = "PLAYWRIGHT_PAGES_DRY_RUN"
ASSET_DIR = SCRIPT_DIR / "assets"
SOURCE_EXTENSIONS = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".js",
    ".jsx",
    ".mjs",
    ".cjs",
    ".ts",
    ".tsx",
    ".py",
    ".sh",
    ".bash",
    ".cmake",
    ".txt",
    ".json",
    ".yaml",
    ".yml",
    ".md",
}


def usage() -> None:
    print("Usage: publish_playwright_pages.py publish|cleanup|restore-videos [site-dir]", file=sys.stderr)


def push_site_branch(site_dir: Path, storage_branch: str) -> bool:
    return common_push_site_branch(
        site_dir,
        storage_branch,
        DRY_RUN_ENV,
        "Update Playwright report pages",
        "Playwright Pages",
    )


def checkout_site_branch(site_dir: Path, storage_branch: str) -> None:
    common_checkout_site_branch(site_dir, storage_branch, DRY_RUN_ENV, "local-playwright-pages")


def script_json(value: str) -> str:
    return value.replace("</", "<\\/")


def copy_asset(site_dir: Path, name: str) -> None:
    source = ASSET_DIR / name
    if not source.is_file():
        raise PublishError(f"Missing Playwright Pages asset: {source}")
    destination = site_dir / REPORT_ROOT / name
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)


def write_index_assets(site_dir: Path) -> None:
    favicon = site_dir / "favicon.svg"
    if favicon.exists():
        favicon.unlink()
    copy_asset(site_dir, "report-index.css")


def write_report_shell_assets(site_dir: Path) -> None:
    copy_asset(site_dir, "report-shell.css")
    copy_asset(site_dir, "report-shell.js")


def source_file_list(head_sha: str) -> list[str]:
    if not head_sha:
        return capture(["git", "ls-files"]).splitlines()
    if run_maybe(["git", "cat-file", "-e", f"{head_sha}^{{tree}}"], quiet=True).returncode == 0:
        return capture(["git", "ls-tree", "-r", "--name-only", head_sha]).splitlines()
    if run_maybe(["git", "fetch", "--depth=1", "origin", head_sha], quiet=True).returncode == 0:
        if run_maybe(["git", "cat-file", "-e", f"{head_sha}^{{tree}}"], quiet=True).returncode == 0:
            return capture(["git", "ls-tree", "-r", "--name-only", head_sha]).splitlines()
    return capture(["git", "ls-files"]).splitlines()


def is_source_path(path: str) -> bool:
    file_path = Path(path)
    return file_path.name == "CMakeLists.txt" or file_path.suffix in SOURCE_EXTENSIONS


def build_source_map(paths: list[str]) -> dict[str, str]:
    files = {}
    basename_counts = {}
    basename_paths = {}
    for path in paths:
        if not is_source_path(path):
            continue
        normalized = path.strip()
        if not normalized:
            continue
        base = Path(normalized).name
        files[normalized] = normalized
        basename_counts[base] = basename_counts.get(base, 0) + 1
        basename_paths[base] = normalized

    source_map = dict(files)
    for base, count in basename_counts.items():
        if count == 1:
            source_map.setdefault(base, basename_paths[base])
    return source_map


def build_source_map_json(head_sha: str) -> str:
    return json.dumps(build_source_map(source_file_list(head_sha)), separators=(",", ":"), sort_keys=True)


def write_index_head(title: str, css_href: str) -> str:
    return f"""<!doctype html>
<html lang="en" style="scrollbar-gutter: stable both-edges;">
  <head>
    <meta charset="utf-8">
    <meta name="color-scheme" content="dark light">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{html_escape(title)}</title>
    <link rel="stylesheet" href="{html_escape(css_href)}">
  </head>
  <body>
    <main>
"""


def write_index_footer() -> str:
    return """    </main>
  </body>
</html>
"""


def write_report_index(report_dir: Path, title: str, back_href: str) -> None:
    parts = [
        write_index_head(f"{title} - Playwright report", f"{back_href}report-index.css"),
        """      <header>
        <div class="eyebrow">Playwright</div>
        <h1>""",
        html_escape(title),
        """</h1>
      </header>
      <section>
        <h2>Report sections</h2>
        <div class="report-list">
""",
    ]
    for phase in sorted(report_dir.iterdir()):
        if not (phase / INDEX_HTML).is_file():
            continue
        phase_name = phase.name
        parts.append(
            '          <a class="report-link" href="'
            f'{html_escape(phase_name)}/{INDEX_HTML}"><span><span class="report-title">{html_escape(phase_name)}</span>'
            '<span class="report-meta">Playwright report</span></span><span class="badge">Open</span></a>\n'
        )
    parts.extend(
        [
            """        </div>
      </section>
""",
            f'      <a class="back-link" href="{html_escape(back_href)}{INDEX_HTML}">Back to report index</a>\n',
            write_index_footer(),
        ]
    )
    (report_dir / INDEX_HTML).write_text("".join(parts), encoding="utf-8")


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


def read_first_line(path: Path, default: str) -> str:
    if not path.is_file():
        return default
    lines = path.read_text(encoding="utf-8").splitlines()
    return lines[0] if lines else default


def write_site_index(site_dir: Path, repository: str) -> None:
    root = site_dir / REPORT_ROOT
    root.mkdir(parents=True, exist_ok=True)
    parts = [
        write_index_head(f"{PRODUCT_TITLE} - Playwright reports", "report-index.css"),
        """      <header>
        <div class="eyebrow">Playwright reports</div>
        <h1>Arm Perception kit</h1>
      </header>
      <section>
        <h2>Nightly</h2>
        <div class="report-list">
""",
    ]
    nightly_found = False
    for directory, title in (("nightly", "General"), ("nightly-macos", "macOS")):
        nightly = root / directory
        if not (nightly / INDEX_HTML).is_file():
            continue
        nightly_found = True
        meta = read_first_line(nightly / REPORT_INDEX_META, "Scheduled develop run")
        parts.append(
            f'          <a class="report-link" href="{directory}/{INDEX_HTML}"><span>'
            f'<span class="report-title">{title}</span><span class="report-meta">'
            f'{html_escape(meta)}</span></span><span class="badge">Open</span></a>\n'
        )
    if not nightly_found:
        parts.append('          <div class="empty">No nightly report published yet.</div>\n')

    parts.append(
        """        </div>
      </section>
      <section>
        <h2>Pull Requests</h2>
        <div class="report-list">
"""
    )
    prs_dir = root / "prs"
    if prs_dir.is_dir():
        pr_dirs = [path for path in prs_dir.iterdir() if path.is_dir() and path.name.isdigit()]
        for pr_dir in sorted(pr_dirs, key=lambda path: int(path.name)):
            if not (pr_dir / INDEX_HTML).is_file():
                continue
            meta = read_first_line(pr_dir / REPORT_INDEX_META, "Published report")
            title = pr_report_title(pr_dir.name, repository)
            parts.append(
                f'          <a class="report-link" href="prs/{html_escape(pr_dir.name)}/{INDEX_HTML}"><span>'
                f'<span class="report-title">{html_escape(title)}</span>'
                f'<span class="report-meta">{html_escape(meta)}</span></span><span class="badge">Open</span></a>\n'
            )

    parts.extend(
        [
            """        </div>
      </section>
""",
            write_index_footer(),
        ]
    )
    (root / INDEX_HTML).write_text("".join(parts), encoding="utf-8")


def inject_once(pattern: str, replacement, content: str, label: str) -> str:
    regex = re.compile(pattern, flags=re.IGNORECASE)
    match = regex.search(content)
    if not match:
        raise PublishError(f"Cannot decorate Playwright report: missing {label}.")
    value = replacement(match) if callable(replacement) else replacement
    return f"{content[:match.start()]}{value}{content[match.end():]}"


def decorate_playwright_report(
    report_dir: Path,
    title: str,
    back_href: str,
    meta_html: str,
    repository: str,
    head_sha: str,
    source_map_json: str,
) -> bool:
    index_file = report_dir / INDEX_HTML
    if not index_file.is_file():
        return False

    content = index_file.read_text(encoding="utf-8")
    if 'class="pek-report-bar"' in content:
        return False

    css_href = f"{back_href}report-shell.css"
    js_href = f"{back_href}report-shell.js"
    page_title = html_escape(f"{title} - Playwright report")
    report_bar = (
        f'    <script type="application/json" id="pek-report-source-map">{script_json(source_map_json)}</script>\n'
        f'    <div class="pek-report-bar" data-repository="{html_escape(repository)}" '
        f'data-commit="{html_escape(head_sha)}"><div class="pek-report-bar-inner">'
        f'<div class="pek-report-info"><span class="pek-report-title">{html_escape(title)}</span>'
        f'<span class="pek-report-meta">{meta_html}</span></div>'
        f'<a class="pek-report-back" href="{html_escape(back_href)}{INDEX_HTML}">Back to report index</a></div></div>'
    )

    content = inject_once(r"<title>.*?</title>", f"<title>{page_title}</title>", content, "<title>")
    content = inject_once(
        r"</head>",
        f'    <link rel="stylesheet" href="{html_escape(css_href)}">\n'
        f'    <script src="{html_escape(js_href)}" defer></script>\n  </head>',
        content,
        "</head>",
    )
    content = inject_once(r"(<body[^>]*>)", lambda match: f"{match.group(1)}\n{report_bar}", content, "<body>")
    index_file.write_text(content, encoding="utf-8")
    return True


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


def build_report_meta_html(repository: str, event: str, pr_number: str, branch: str, head_sha: str,
                           run_id: str, run_attempt: str) -> str:
    if event == "pull_request":
        prefix = html_anchor(f"https://github.com/{repository}/pull/{pr_number}", f"PR #{pr_number}")
    else:
        prefix = "Nightly"
    return f"{prefix} | {build_source_meta_html(repository, branch, head_sha, run_id, run_attempt)}"


def build_source_meta_text(branch: str, head_sha: str, run_id: str, run_attempt: str) -> str:
    return f"{branch} @ {head_sha[:12]} | run {run_id} attempt {run_attempt}"


def build_report_index_meta_text(branch: str, head_sha: str, run_id: str, run_attempt: str,
                                 now: dt.datetime | None = None) -> str:
    if now is None:
        now = dt.datetime.now(dt.timezone.utc)
    return f"{build_source_meta_text(branch, head_sha, run_id, run_attempt)} | {now.strftime('%b %d, %Y %H:%M UTC')}"


def download_report_artifact(
    artifact_dir: Path,
    repository: str,
    run_id: str,
    run_attempt: str,
    artifact_prefix: str = DEFAULT_ARTIFACT_PREFIX,
) -> bool:
    local_report_dir = env("PLAYWRIGHT_PAGES_LOCAL_REPORT_DIR")
    if local_report_dir:
        source = Path(local_report_dir)
        if not (source / INDEX_HTML).is_file():
            raise PublishError(f"Local Playwright report not found: {source}")
        target = artifact_dir / "local-artifact" / "playwright-report"
        shutil.copytree(source, target)
        return True

    artifact_name = f"{artifact_prefix}-{run_id}-{run_attempt}"
    result = subprocess.run(
        ["gh", "run", "download", run_id, "--repo", repository, "--name", artifact_name, "--dir", str(artifact_dir)],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    if result.returncode == 0:
        return True
    print(f"No Playwright browser smoke artifact found for run {run_id} attempt {run_attempt}.")
    return False


def find_playwright_report(artifact_dir: Path) -> Path | None:
    for path in artifact_dir.rglob("playwright-report"):
        if path.is_dir():
            return path
    return None


def prune_report_for_pages(report_dir: Path) -> None:
    for path in report_dir.rglob("data/*"):
        if path.is_file() and path.suffix in PRUNED_REPORT_DATA_SUFFIXES:
            path.unlink()


def is_pruned_report_data(path: Path) -> bool:
    return path.parent.name == "data" and path.suffix in PRUNED_REPORT_DATA_SUFFIXES


def copy_pruned_report_for_pages(report_dir: Path, target: Path) -> None:
    copy_report(report_dir, target)
    prune_report_for_pages(target)


def report_video_files(report_dir: Path) -> list[str]:
    return sorted(
        path.relative_to(report_dir).as_posix()
        for path in report_dir.rglob("data/*.webm")
        if path.is_file()
    )


def write_video_artifact_meta(target: Path, run_id: str, run_attempt: str, files: list[str]) -> None:
    (target / VIDEO_ARTIFACT_META).write_text(
        json.dumps({"files": files, "run_attempt": run_attempt, "run_id": run_id}, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def read_video_artifact_meta(path: Path) -> tuple[int, int, list[str]] | None:
    if not path.is_file():
        return None
    try:
        metadata = json.loads(path.read_text(encoding="utf-8"))
        run_id = metadata["run_id"]
        run_attempt = metadata["run_attempt"]
        files = metadata["files"]
        if not isinstance(run_id, str) or not run_id.isdigit():
            raise ValueError("invalid run ID")
        if not isinstance(run_attempt, str) or not run_attempt.isdigit():
            raise ValueError("invalid run attempt")
        if not isinstance(files, list) or len(files) != len(set(files)):
            raise ValueError("invalid video file list")
        for filename in files:
            video_path = Path(filename) if isinstance(filename, str) else Path()
            if (not isinstance(filename, str) or video_path.is_absolute() or ".." in video_path.parts
                    or video_path.parent.name != "data" or video_path.suffix != ".webm"):
                raise ValueError("invalid video file path")
    except (KeyError, OSError, TypeError, ValueError, json.JSONDecodeError) as error:
        print(f"Ignoring invalid Playwright video metadata at {path}: {error}")
        return None
    return int(run_id), int(run_attempt), files


def read_published_report_meta(target: Path) -> tuple[int, int, list[str] | None] | None:
    metadata = read_video_artifact_meta(target / VIDEO_ARTIFACT_META)
    if metadata is not None:
        return metadata
    match = re.search(
        r"\| run (\d+) attempt (\d+)(?: \||$)",
        read_first_line(target / REPORT_INDEX_META, ""),
    )
    return (int(match.group(1)), int(match.group(2)), None) if match else None


def is_stale_report(target: Path, run_id: str, run_attempt: str) -> bool:
    published = read_published_report_meta(target)
    return published is not None and (int(run_id), int(run_attempt)) < published[:2]


def restore_report_videos_for_deploy(
    report_dir: Path,
    target: Path,
    files: list[str] | None = None,
) -> int:
    count = 0
    sources = report_dir.rglob("data/*.webm") if files is None else (report_dir / filename for filename in files)
    for source in sources:
        if source.is_file():
            destination = target / source.relative_to(report_dir)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
            count += 1
    return count


def restore_published_report_videos(site_dir: Path) -> int:
    repository = require_env("GITHUB_REPOSITORY")
    targets = sorted(path.parent for path in (site_dir / REPORT_ROOT).glob(f"**/{REPORT_INDEX_META}"))
    restored = 0
    with tempfile.TemporaryDirectory() as tmpdir:
        for index, target in enumerate(targets):
            metadata = read_published_report_meta(target)
            if metadata is None:
                continue
            run_id, run_attempt, files = metadata
            if files is not None and (not files or all((target / filename).is_file() for filename in files)):
                continue

            artifact_dir = Path(tmpdir) / str(index)
            artifact_prefix = read_first_line(target / ARTIFACT_PREFIX_META, DEFAULT_ARTIFACT_PREFIX)
            if not download_report_artifact(
                artifact_dir, repository, str(run_id), str(run_attempt), artifact_prefix
            ):
                print(f"Could not restore Playwright videos from run {run_id}; keeping the run link.")
                continue
            report_dir = find_playwright_report(artifact_dir)
            if report_dir is None:
                print(f"Run {run_id} artifact did not contain playwright-report; keeping the run link.")
                continue
            try:
                validate_report_for_pages(report_dir)
            except PublishError as error:
                print(f"Could not restore Playwright videos from run {run_id}: {error}")
                continue
            if files is None:
                files = report_video_files(report_dir)
            if any(not (report_dir / filename).is_file() for filename in files):
                print(f"Run {run_id} artifact is missing a Playwright video; keeping the run link.")
                continue
            restored += restore_report_videos_for_deploy(report_dir, target, files)
    return restored


def validate_report_for_pages(report_dir: Path) -> None:
    total = 0
    for path in report_dir.rglob("*"):
        if path.is_symlink():
            raise PublishError(f"Playwright report contains unsupported symlink: {path.relative_to(report_dir)}")
        if path.is_file() and not is_pruned_report_data(path):
            total += path.stat().st_size
            if total > MAX_REPORT_BYTES:
                raise PublishError(
                    f"Playwright report exceeds publish limit after pruning: {total} bytes > {MAX_REPORT_BYTES} bytes"
                )


def copy_report(report_dir: Path, target: Path) -> None:
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    shutil.copytree(report_dir, target, dirs_exist_ok=True)


def publish_report(site_dir: Path, storage_branch: str) -> None:
    repository = require_env("GITHUB_REPOSITORY")
    event = require_env("UPSTREAM_EVENT")
    branch = require_env("UPSTREAM_HEAD_BRANCH")
    head_sha = require_env("UPSTREAM_HEAD_SHA")
    conclusion = require_env("UPSTREAM_CONCLUSION")
    run_id = require_env("UPSTREAM_RUN_ID")
    run_attempt = require_env("UPSTREAM_RUN_ATTEMPT")
    artifact_prefix = env("PLAYWRIGHT_PAGES_ARTIFACT_PREFIX", DEFAULT_ARTIFACT_PREFIX)
    report_title = env("PLAYWRIGHT_PAGES_REPORT_TITLE", PRODUCT_TITLE)

    if not run_id.isdigit() or not run_attempt.isdigit():
        raise PublishError("UPSTREAM_RUN_ID and UPSTREAM_RUN_ATTEMPT must be numeric.")
    if not re.fullmatch(r"[A-Za-z0-9._-]+", artifact_prefix):
        raise PublishError(f"Invalid Playwright artifact prefix: {artifact_prefix}")

    if conclusion not in {"success", "failure"}:
        print(f"Skipping Playwright report from {conclusion} upstream run.")
        set_output("deploy", "false")
        return

    if event == "pull_request":
        head_repository = env("UPSTREAM_HEAD_REPOSITORY")
        if head_repository and head_repository != repository:
            print(f"Skipping PR Playwright report from untrusted repository: {head_repository}.")
            set_output("deploy", "false")
            return
        pr_number = env("UPSTREAM_PR_NUMBER")
        if not pr_number:
            print("No PR number found for upstream run; skipping Pages publish.")
            set_output("deploy", "false")
            return
        root = site_dir / REPORT_ROOT
        target = root / "prs" / pr_number
        back_href = "../../"
    else:
        if event != "schedule" or branch != "develop":
            print(f"Skipping non-PR Playwright report from {event} on {branch}.")
            set_output("deploy", "false")
            return
        nightly_directory = env("PLAYWRIGHT_PAGES_NIGHTLY_DIRECTORY", "nightly")
        if not re.fullmatch(r"[a-z0-9-]+", nightly_directory):
            raise PublishError(f"Invalid Playwright nightly directory: {nightly_directory}")
        pr_number = ""
        root = site_dir / REPORT_ROOT
        target = root / nightly_directory
        back_href = "../"

    index_meta_text = build_report_index_meta_text(branch, head_sha, run_id, run_attempt)
    meta_html = build_report_meta_html(repository, event, pr_number, branch, head_sha, run_id, run_attempt)
    source_meta_html = build_source_meta_html(repository, branch, head_sha, run_id, run_attempt)

    checkout_site_branch(site_dir, storage_branch)
    if is_stale_report(target, run_id, run_attempt):
        print(f"Skipping stale Playwright report from run {run_id} attempt {run_attempt}.")
        set_output("deploy", "false")
        return

    with tempfile.TemporaryDirectory() as tmpdir:
        artifact_dir = Path(tmpdir)
        if not download_report_artifact(artifact_dir, repository, run_id, run_attempt, artifact_prefix):
            set_output("deploy", "false")
            return

        report_dir = find_playwright_report(artifact_dir)
        if report_dir is None:
            print("Artifact did not contain playwright-report; skipping Pages publish.")
            set_output("deploy", "false")
            return

        validate_report_for_pages(report_dir)
        video_files = report_video_files(report_dir)
        remove_legacy_root_site(site_dir)
        copy_pruned_report_for_pages(report_dir, target)
        write_video_artifact_meta(target, run_id, run_attempt, video_files)
        (target / ARTIFACT_PREFIX_META).write_text(f"{artifact_prefix}\n", encoding="utf-8")
        (target / REPORT_INDEX_META).write_text(f"{index_meta_text}\n", encoding="utf-8")
        (target / "report-meta.html").write_text(f"{meta_html}\n", encoding="utf-8")
        (target / "report-source-meta.html").write_text(f"{source_meta_html}\n", encoding="utf-8")
        write_report_shell_assets(site_dir)
        if not (target / INDEX_HTML).is_file():
            write_report_index(target, report_title, back_href)
        else:
            decorate_playwright_report(
                target,
                report_title,
                back_href,
                meta_html,
                repository,
                head_sha,
                build_source_map_json(head_sha),
            )
        (target / "commit.txt").write_text(f"{head_sha}\n", encoding="utf-8")
        (site_dir / ".nojekyll").touch()
        write_root_index(site_dir)
        write_index_assets(site_dir)
        write_site_index(site_dir, repository)

        changed = push_site_branch(site_dir, storage_branch)
        restored_videos = restore_report_videos_for_deploy(report_dir, target, video_files)
        set_output("deploy", "true" if changed or restored_videos else "false")


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


def cleanup_closed_pr_reports(site_dir: Path, storage_branch: str, retention_days: int) -> None:
    repository = require_env("GITHUB_REPOSITORY")
    checkout_site_branch(site_dir, storage_branch)
    cutoff = dt.datetime.now(dt.timezone.utc) - dt.timedelta(days=retention_days)
    changed = remove_legacy_root_site(site_dir)

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
        write_root_index(site_dir)
        write_index_assets(site_dir)
        write_site_index(site_dir, repository)
        pushed = push_site_branch(site_dir, storage_branch)
        set_output("deploy", "true" if pushed else "false")
    else:
        set_output("deploy", "false")


def parse_retention_days(value: str) -> int:
    try:
        days = int(value)
    except ValueError as error:
        raise PublishError(f"PLAYWRIGHT_PAGES_RETENTION_DAYS must be an integer: {value}") from error
    if days < 0:
        raise PublishError("PLAYWRIGHT_PAGES_RETENTION_DAYS must be zero or greater.")
    return days


def main(argv: list[str]) -> int:
    if len(argv) == 3 and argv[1] == "restore-videos":
        try:
            restore_published_report_videos(Path(argv[2]))
        except PublishError as error:
            print(f"Error: {error}", file=sys.stderr)
            return 1
        return 0
    if len(argv) != 2 or argv[1] not in {"publish", "cleanup"}:
        usage()
        return 2

    storage_branch = env("PLAYWRIGHT_PAGES_STORAGE_BRANCH", "playwright-pages")
    site_dir = Path(env("PLAYWRIGHT_PAGES_SITE_DIR", "_playwright_pages_site"))
    retention_days = parse_retention_days(env("PLAYWRIGHT_PAGES_RETENTION_DAYS", "10"))

    try:
        if argv[1] == "publish":
            retry_storage_branch_update(lambda: publish_report(site_dir, storage_branch))
        else:
            retry_storage_branch_update(
                lambda: cleanup_closed_pr_reports(site_dir, storage_branch, retention_days)
            )
    except PublishError as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
