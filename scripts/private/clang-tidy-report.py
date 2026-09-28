#!/usr/bin/env python3
################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################
"""Run local clang-tidy analysis for Meson and render a Markdown report."""

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile


# Match the GCC compatibility adjustments used by the existing CI analysis.
FILTERED_FLAGS = {"-fno-reorder-functions", "-mfp16-format=ieee", "-fno-defer-pop"}
DIAGNOSTIC = re.compile(
    r"^(.+?):(\d+):(\d+): (warning|error|fatal error): (.+)$", re.MULTILINE)
CHECK = re.compile(r" \[([^\]]+)\]$")
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".C"}
HEADER_SUFFIXES = {".h", ".hh", ".hpp", ".hxx", ".tpp", ".ipp", ".inl", ".inc"}
HUNK = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", re.MULTILINE)


def git(repository, *arguments, optional=False):
    result = subprocess.run(["git", "-C", str(repository), *arguments],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, encoding="utf-8", errors="surrogateescape", check=False)
    if result.returncode:
        if optional:
            return ""
        raise ValueError(f"Git comparison failed: {result.stderr.strip()}")
    return result.stdout


def changed_lines(repository, build_directory, base_ref):
    """Compare the merge base to tracked working-tree content, preserving renames."""
    if not base_ref:
        branch = git(repository, "symbolic-ref", "--quiet", "--short", "HEAD", optional=True).strip()
        if branch:
            base_ref = git(repository, "config", "--get", f"branch.{branch}.vscode-merge-base",
                           optional=True).strip()
        if not base_ref:
            base_ref = git(repository, "symbolic-ref", "--quiet", "refs/remotes/origin/HEAD",
                           optional=True).strip()
    if not base_ref:
        raise ValueError("No comparison reference found. Set CLANG_TIDY_BASE, for example origin/develop.")
    candidates = [base_ref]
    if not base_ref.startswith(("refs/", "origin/")):
        candidates.insert(0, f"refs/remotes/origin/{base_ref}")
    target = ""
    for candidate in candidates:
        target = git(repository, "rev-parse", "--verify", "--end-of-options", candidate + "^{commit}",
                     optional=True).strip()
        if target:
            base_ref = candidate
            break
    if not target:
        raise ValueError(f"Cannot resolve comparison reference {base_ref!r}. Set CLANG_TIDY_BASE to a local ref.")
    merge_base = git(repository, "merge-base", "HEAD", target, optional=True).strip()
    if not merge_base:
        raise ValueError(f"No merge base found with {base_ref}")
    comparison = (f"Reference: {base_ref}\nTarget commit: {target}\nMerge base: {merge_base}\n"
                  "Compared with: tracked working tree (including staged and unstaged changes)")
    diff_args = ["diff", "--no-ext-diff", "--no-textconv", "--no-color", "--no-relative", "--find-renames"]
    paths = iter(git(repository, *diff_args, "--name-status", "-z", "--diff-filter=ACMRD",
                     merge_base, "--", "development").split("\0")[:-1])
    changes = {}
    headers_changed = False
    for status in paths:
        old_path = next(paths)
        path = next(paths) if status.startswith(("R", "C")) else old_path
        # Deletions and renames can affect includers even without added header lines.
        headers_changed |= any(
            Path(name).suffix in HEADER_SUFFIXES
            and project_path(repository / name, repository, build_directory)
            for name in (old_path, path)
        )
        absolute = repository / path
        if (absolute.suffix not in SOURCE_SUFFIXES | HEADER_SUFFIXES
                or not project_path(absolute, repository, build_directory)):
            continue
        # Read hunks separately so Git-quoted paths, spaces and renames need no patch-header parsing.
        patch = git(repository, *diff_args, "--unified=0", "--inter-hunk-context=0",
                    "--diff-algorithm=myers", merge_base, "--",
                    f":(literal){old_path}", f":(literal){path}")
        ranges = []
        for match in HUNK.finditer(patch):
            start = int(match.group(1))
            count = int(match.group(2)) if match.group(2) is not None else 1
            if count:
                ranges.append((start, start + count - 1))
        if ranges:
            changes[absolute.resolve().relative_to(repository).as_posix()] = ranges
    return changes, comparison, headers_changed


def project_path(path, repository, build_directory):
    """Keep authored runtime sources/headers, excluding builds and dependencies."""
    path = path.resolve()
    source = repository / "development"
    if not path.is_relative_to(source) or path.is_relative_to(build_directory):
        return False
    first = path.relative_to(source).parts[0]
    return first != "subprojects" and not first.startswith("build")


def prepare_database(repository, build_directory, destination):
    database = json.loads((build_directory / "compile_commands.json").read_text())
    if not isinstance(database, list) or not all(isinstance(entry, dict) for entry in database):
        raise ValueError("Compilation database must be an array of command objects")
    entries = []
    files = {}
    for entry in database:
        directory = (build_directory / entry["directory"]).resolve()
        path = (directory / entry["file"]).resolve()
        if path.suffix not in SOURCE_SUFFIXES or not project_path(path, repository, build_directory):
            continue
        args = entry.get("arguments")
        if args is None:
            args = shlex.split(entry["command"])
        entries.append({
            **{key: value for key, value in entry.items() if key != "command"},
            "directory": str(directory),
            "file": str(path),
            "arguments": [arg for arg in args if arg not in FILTERED_FLAGS],
        })
        # clang-tidy processes all commands for this file in the database.
        files.setdefault(path, directory)
    if not files:
        raise ValueError("No project C/C++ sources found in the compilation database")
    (destination / "compile_commands.json").write_text(json.dumps(entries), encoding="utf-8")
    return sorted(files.items())


def header_filter(repository, build_directory):
    source = repository / "development"
    folders = sorted(re.escape(path.name) for path in source.iterdir()
                     if path.is_dir() and project_path(path, repository, build_directory))
    prefixes = [re.escape(str(source)), re.escape(os.path.relpath(source, build_directory))]
    return "^(" + "|".join(prefixes) + ")/(" + "|".join(folders) + ")/"


def select_new_files(files, changes, headers_changed, repository):
    """Avoid launching clang-tidy on unchanged TUs unless headers may affect them."""
    if not changes:
        return [], "No added or modified C/C++ lines; skipping analysis."
    if headers_changed:
        return files, f"Header changes detected; analyzing all {len(files)} configured translation units."
    selected = [(path, directory) for path, directory in files
                if path.relative_to(repository).as_posix() in changes]
    message = f"Source-only changes: selected {len(selected)} of {len(files)} configured translation units."
    if len(selected) < len(changes):
        message += " Changed sources outside the active compilation database are skipped; reconfigure to include them."
    return selected, message


def parse_diagnostics(output, directory, repository, build_directory):
    matches = list(DIAGNOSTIC.finditer(output))
    for index, match in enumerate(matches):
        path, line, column, severity, message = match.groups()
        path = (directory / path).resolve()
        if not project_path(path, repository, build_directory):
            continue
        check = CHECK.search(message)
        check_name = check.group(1) if check else "compiler"
        relative = path.relative_to(repository).as_posix()
        key = (relative, int(line), int(column), severity, message)
        end = matches[index + 1].start() if index + 1 < len(matches) else len(output)
        # Keep source excerpts and analyzer notes with their primary diagnostic.
        detail = output[match.end():end].rstrip()
        text = f"{relative}:{line}:{column}: {severity}: {message}"
        if detail:
            text += "\n" + detail.lstrip("\n")
        yield key, check_name, text


def fenced(text):
    fence = "`" * max(3, 1 + max((len(m.group()) for m in re.finditer(r"`+", text)), default=0))
    return f"{fence}text\n{text}\n{fence}\n"


def write_report(report, repository, build_directory, version, total, completed, findings, failures,
                 scope="all", comparison=""):
    incomplete = bool(failures) or completed != total
    status = "INCOMPLETE" if incomplete else "Complete"
    counts = Counter(check for check, _ in findings.values())
    lines = [
        f"# clang-tidy: {'new code' if scope == 'new' else 'overall code'}\n",
        f"Status: {status}\n",
        fenced(f"Tool: {version}\nRepository: {repository}\nBuild: {build_directory}"),
        f"Translation units processed: {completed}/{total}. Unique findings: {len(findings)}.\n",
        "Scope: authored C/C++ sources in the active compilation database and their project headers. "
        "Dependencies, generated SDK sources, and disabled build components are excluded. "
        "Warnings are advisory; tool and compilation failures make the report incomplete.\n",
    ]
    if scope == "new":
        lines.append("Only findings whose primary location is on an added or modified C/C++ line are shown. "
                     "This is a changed-line view, not a comparison of diagnostics between two builds.\n")
        lines.append(fenced(comparison or "Comparison unavailable."))
    lines.append("## Findings by check\n")
    lines.extend(f"- {name}: {count}" for name, count in sorted(counts.items()))
    lines.append("\n## Findings\n")
    if not findings:
        lines.append("No project findings were reported." if failures else "No findings.")
    lines.extend(fenced(findings[key][1]) for key in sorted(findings))
    if failures:
        lines.append("\n## Analysis failures\n")
        lines.extend(fenced(failure) for failure in failures)
        lines.append(f"See the sibling clang-tidy-{scope}.log for the complete tool output.")
    report.write_text("\n".join(lines) + "\n", encoding="utf-8")


def run(args):
    repository = args.repository.resolve()
    build_directory = args.build_directory.resolve()
    output = build_directory / "meson-logs"
    output.mkdir(parents=True, exist_ok=True)
    report = output / f"clang-tidy-{args.scope}.md"
    comparison = ""
    changes = None
    headers_changed = False
    findings = {}
    failures = []
    total = completed = 0
    version = "unavailable"
    # Replace the old report immediately so failed runs never leave a stale success.
    write_report(report, repository, build_directory, version, total, completed,
                 findings, ["Analysis has not completed."], args.scope, comparison)
    with report.with_suffix(".log").open("w", encoding="utf-8") as log:
        try:
            if args.scope == "new":
                changes, comparison, headers_changed = changed_lines(repository, build_directory, args.base)
                print(comparison, flush=True)
                log.write(comparison + "\n")
            binary = shutil.which(args.clang_tidy)
            if binary is None:
                raise ValueError("clang-tidy was not found; run in the development container "
                                 "or set CLANG_TIDY to its executable")
            probe = subprocess.run([binary, "--version"], capture_output=True, text=True, check=True)
            version = probe.stdout.strip()
            if not (repository / ".clang-tidy").is_file():
                raise ValueError(f"Missing policy: {repository / '.clang-tidy'}")
            with tempfile.TemporaryDirectory(prefix="opk-clang-tidy-") as temporary:
                database = Path(temporary)
                files = prepare_database(repository, build_directory, database)
                if changes is not None:
                    files, selection = select_new_files(files, changes, headers_changed, repository)
                    comparison += "\n" + selection
                    print(selection, flush=True)
                    log.write(selection + "\n")
                headers = header_filter(repository, build_directory)
                total = len(files)
                print(f"clang-tidy: analyzing {total} translation units with {args.jobs} workers", flush=True)

                def analyze(item):
                    path, directory = item
                    return subprocess.run(
                        [binary, str(path), "-p", str(database),
                         f"--config-file={repository / '.clang-tidy'}",
                         f"--header-filter={headers}", "--use-color=false", "--quiet",
                         "--extra-arg=-DFMT_CONSTEVAL="],
                        cwd=directory, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        text=True, errors="replace", check=False,
                    )

                with ThreadPoolExecutor(max_workers=args.jobs) as executor:
                    pending = {executor.submit(analyze, item): item for item in files}
                    for future in as_completed(pending):
                        path, directory = pending[future]
                        completed += 1
                        label = path.relative_to(repository).as_posix()
                        try:
                            result = future.result()
                        except OSError as exc:
                            failures.append(f"{label}: {exc}")
                            print(failures[-1], flush=True)
                            continue
                        log.write(f"\n--- {label} (exit {result.returncode}) ---\n{result.stdout}\n")
                        log.flush()
                        for key, check, detail in parse_diagnostics(
                                result.stdout, directory, repository, build_directory):
                            if changes is not None and not any(
                                    start <= key[1] <= end for start, end in changes.get(key[0], [])):
                                continue
                            if key not in findings:
                                findings[key] = (check, detail)
                                print(detail, flush=True)
                        if result.returncode:
                            failures.append(f"{label}: clang-tidy exited with {result.returncode}\n{result.stdout}")
                            print(failures[-1], flush=True)
                        print(f"clang-tidy: {completed}/{total} processed", flush=True)
        except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as exc:
            failures.append(str(exc))
            print(f"clang-tidy: {exc}", flush=True)
        except KeyboardInterrupt:
            failures.append("Analysis interrupted.")
        finally:
            write_report(report, repository, build_directory, version, total, completed, findings, failures,
                         args.scope, comparison)
    print(f"clang-tidy: {len(findings)} unique findings; "
          f"{'INCOMPLETE' if failures else 'complete'}\nReport: {report}", flush=True)
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--build-directory", type=Path, required=True)
    parser.add_argument("--scope", choices=("all", "new"), default="all")
    parser.add_argument("--base", default=os.environ.get("CLANG_TIDY_BASE") or
                        os.environ.get("PULL_REQUEST_TARGET_BRANCH"),
                        help="Comparison ref for new code; otherwise use branch configuration or origin/HEAD")
    parser.add_argument("--clang-tidy", default=os.environ.get("CLANG_TIDY", "clang-tidy"))
    parser.add_argument("--jobs", type=int, default=os.environ.get("CLANG_TIDY_JOBS", "4"))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
