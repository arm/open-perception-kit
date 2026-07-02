#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
# Updates the persisted Playwright report site used by GitHub Pages.
################################################################

import base64
import datetime as dt
import html
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


PRODUCT_TITLE = "Arm Perception kit"
SCRIPT_DIR = Path(__file__).resolve().parent
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


class PublishError(RuntimeError):
    pass


def usage() -> None:
    print("Usage: scripts/playwright/pages/publish.sh publish|cleanup", file=sys.stderr)


def env(name: str, default: str = "") -> str:
    return os.environ.get(name, default)


def require_env(name: str) -> str:
    value = env(name)
    if not value:
        raise PublishError(f"{name} is required.")
    return value


def dry_run_enabled() -> bool:
    return env("PLAYWRIGHT_PAGES_DRY_RUN") == "1"


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


def capture(args: list[str], cwd: Path | None = None) -> str:
    result = subprocess.run(args, cwd=cwd, check=True, stdout=subprocess.PIPE, text=True)
    return result.stdout


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
        run(["git", "-C", str(site_dir), "config", "user.name", "local-playwright-pages"])
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

    run(["git", "-C", str(site_dir), "commit", "-m", "Update Playwright report pages"])
    if dry_run_enabled():
        print(f"Dry-run: generated Playwright Pages site at {site_dir}")
        return True

    auth_header = git_auth_header()
    git_with_auth(["push", "origin", f"HEAD:{storage_branch}"], site_dir, auth_header)
    return True


def html_escape(value: str) -> str:
    return html.escape(str(value), quote=True)


def html_anchor(href: str, text: str) -> str:
    return f'<a href="{html_escape(href)}">{html_escape(text)}</a>'


def copy_asset(site_dir: Path, name: str) -> None:
    source = ASSET_DIR / name
    if not source.is_file():
        raise PublishError(f"Missing Playwright Pages asset: {source}")
    shutil.copyfile(source, site_dir / name)


def write_index_assets(site_dir: Path) -> None:
    favicon = site_dir / "favicon.svg"
    if favicon.exists():
        favicon.unlink()
    copy_asset(site_dir, "report-index.css")


def write_report_shell_assets(site_dir: Path) -> None:
    copy_asset(site_dir, "report-shell.css")
    copy_asset(site_dir, "report-shell.js")


def source_file_list(head_sha: str) -> list[str]:
    if head_sha:
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
        if not (phase / "index.html").is_file():
            continue
        phase_name = phase.name
        parts.append(
            '          <a class="report-link" href="'
            f'{html_escape(phase_name)}/"><span><span class="report-title">{html_escape(phase_name)}</span>'
            '<span class="report-meta">Playwright report</span></span><span class="badge">Open</span></a>\n'
        )
    parts.extend(
        [
            """        </div>
      </section>
""",
            f'      <a class="back-link" href="{html_escape(back_href)}">Back to report index</a>\n',
            write_index_footer(),
        ]
    )
    (report_dir / "index.html").write_text("".join(parts), encoding="utf-8")


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
    return path.read_text(encoding="utf-8").splitlines()[0]


def write_site_index(site_dir: Path, repository: str) -> None:
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
    nightly = site_dir / "nightly"
    if (nightly / "index.html").is_file():
        meta = read_first_line(nightly / "report-index-meta.txt", "Scheduled main run")
        parts.append(
            '          <a class="report-link" href="nightly/"><span><span class="report-title">'
            f'Latest nightly</span><span class="report-meta">{html_escape(meta)}</span></span>'
            '<span class="badge">Open</span></a>\n'
        )
    else:
        parts.append('          <div class="empty">No nightly report published yet.</div>\n')

    parts.append(
        """        </div>
      </section>
      <section>
        <h2>Pull Requests</h2>
        <div class="report-list">
"""
    )
    prs_dir = site_dir / "prs"
    if prs_dir.is_dir():
        pr_dirs = [path for path in prs_dir.iterdir() if path.is_dir() and path.name.isdigit()]
        for pr_dir in sorted(pr_dirs, key=lambda path: int(path.name)):
            if not (pr_dir / "index.html").is_file():
                continue
            meta = read_first_line(pr_dir / "report-index-meta.txt", "Published report")
            title = pr_report_title(pr_dir.name, repository)
            parts.append(
                f'          <a class="report-link" href="prs/{html_escape(pr_dir.name)}/"><span>'
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
    (site_dir / "index.html").write_text("".join(parts), encoding="utf-8")


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
    index_file = report_dir / "index.html"
    if not index_file.is_file():
        return False

    content = index_file.read_text(encoding="utf-8")
    if 'class="pek-report-bar"' in content:
        return False

    css_href = f"{back_href}report-shell.css"
    js_href = f"{back_href}report-shell.js"
    page_title = html_escape(f"{title} - Playwright report")
    report_bar = (
        f'    <script type="application/json" id="pek-report-source-map">{source_map_json}</script>\n'
        f'    <div class="pek-report-bar" data-repository="{html_escape(repository)}" '
        f'data-commit="{html_escape(head_sha)}"><div class="pek-report-bar-inner">'
        f'<div class="pek-report-info"><span class="pek-report-title">{html_escape(title)}</span>'
        f'<span class="pek-report-meta">{meta_html}</span></div>'
        f'<a class="pek-report-back" href="{html_escape(back_href)}">Back to report index</a></div></div>'
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
    return "".join(
        [
            html_anchor(f"{repo_url}/tree/{branch}", branch),
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


def download_report_artifact(artifact_dir: Path, repository: str, run_id: str, run_attempt: str) -> bool:
    local_report_dir = env("PLAYWRIGHT_PAGES_LOCAL_REPORT_DIR")
    if local_report_dir:
        source = Path(local_report_dir)
        if not (source / "index.html").is_file():
            raise PublishError(f"Local Playwright report not found: {source}")
        target = artifact_dir / "local-artifact" / "playwright-report"
        shutil.copytree(source, target)
        return True

    artifact_name = f"rpi-browser-smoke-{run_id}-{run_attempt}"
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
    for path in report_dir.glob("*/data/*.zip"):
        if path.is_file():
            path.unlink()


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
        target = site_dir / "prs" / pr_number
        back_href = "../../"
    else:
        if event != "schedule" or branch != "main":
            print(f"Skipping non-PR Playwright report from {event} on {branch}.")
            set_output("deploy", "false")
            return
        pr_number = ""
        target = site_dir / "nightly"
        back_href = "../"

    index_meta_text = build_report_index_meta_text(branch, head_sha, run_id, run_attempt)
    meta_html = build_report_meta_html(repository, event, pr_number, branch, head_sha, run_id, run_attempt)
    source_meta_html = build_source_meta_html(repository, branch, head_sha, run_id, run_attempt)

    with tempfile.TemporaryDirectory() as tmpdir:
        artifact_dir = Path(tmpdir)
        if not download_report_artifact(artifact_dir, repository, run_id, run_attempt):
            set_output("deploy", "false")
            return

        report_dir = find_playwright_report(artifact_dir)
        if report_dir is None:
            print("Artifact did not contain playwright-report; skipping Pages publish.")
            set_output("deploy", "false")
            return

        checkout_site_branch(site_dir, storage_branch)
        copy_report(report_dir, target)
        prune_report_for_pages(target)
        (target / "report-index-meta.txt").write_text(f"{index_meta_text}\n", encoding="utf-8")
        (target / "report-meta.html").write_text(f"{meta_html}\n", encoding="utf-8")
        (target / "report-source-meta.html").write_text(f"{source_meta_html}\n", encoding="utf-8")
        write_report_shell_assets(site_dir)
        if not (target / "index.html").is_file():
            write_report_index(target, PRODUCT_TITLE, back_href)
        else:
            decorate_playwright_report(
                target,
                PRODUCT_TITLE,
                back_href,
                meta_html,
                repository,
                head_sha,
                build_source_map_json(head_sha),
            )
        (target / "commit.txt").write_text(f"{head_sha}\n", encoding="utf-8")
        (site_dir / ".nojekyll").touch()
        write_index_assets(site_dir)
        write_site_index(site_dir, repository)

        push_site_branch(site_dir, storage_branch)
        set_output("deploy", "true")


def parse_github_time(value: str) -> dt.datetime:
    return dt.datetime.fromisoformat(value.replace("Z", "+00:00"))


def should_prune_closed_pr(state: str, closed_at: str, cutoff: dt.datetime) -> bool:
    if state == "OPEN" or not closed_at:
        return False
    return parse_github_time(closed_at) <= cutoff


def cleanup_closed_pr_reports(site_dir: Path, storage_branch: str, retention_days: int) -> None:
    repository = require_env("GITHUB_REPOSITORY")
    checkout_site_branch(site_dir, storage_branch)
    cutoff = dt.datetime.now(dt.timezone.utc) - dt.timedelta(days=retention_days)
    changed = False

    prs_dir = site_dir / "prs"
    if prs_dir.is_dir():
        for pr_dir in prs_dir.iterdir():
            if not pr_dir.is_dir() or not pr_dir.name.isdigit():
                continue
            result = subprocess.run(
                ["gh", "pr", "view", pr_dir.name, "--repo", repository, "--json", "state,closedAt"],
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
            )
            if result.returncode != 0 or not result.stdout.strip():
                continue
            pr_data = json.loads(result.stdout)
            if should_prune_closed_pr(pr_data.get("state", ""), pr_data.get("closedAt", ""), cutoff):
                shutil.rmtree(pr_dir)
                changed = True

    if changed:
        write_index_assets(site_dir)
        write_site_index(site_dir, repository)
        push_site_branch(site_dir, storage_branch)
        set_output("deploy", "true")
    else:
        set_output("deploy", "false")


def main(argv: list[str]) -> int:
    if len(argv) != 2 or argv[1] not in {"publish", "cleanup"}:
        usage()
        return 2

    storage_branch = env("PLAYWRIGHT_PAGES_STORAGE_BRANCH", "playwright-pages")
    site_dir = Path(env("PLAYWRIGHT_PAGES_SITE_DIR", "_playwright_pages_site"))
    retention_days = int(env("PLAYWRIGHT_PAGES_RETENTION_DAYS", "10"))

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
