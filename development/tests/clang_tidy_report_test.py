#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import contextlib
import io
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from argparse import Namespace


REPOSITORY = Path(__file__).resolve().parents[2]
HELPER = REPOSITORY / "scripts/private/clang-tidy-report.py"
REPORTER = runpy.run_path(str(HELPER))


class ClangTidyReportTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="opk-tidy-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "checkout with spaces"
        self.build = self.root / "development/build"
        self.build.mkdir(parents=True)
        self.source = self.root / "development/common"
        self.source.mkdir()
        (self.root / ".clang-tidy").write_text("Checks: '-*,modernize-use-nullptr'\n")
        self.args = Namespace(repository=self.root, build_directory=self.build,
                              clang_tidy="clang-tidy", jobs=2, scope="all", base=None)
        self.report = self.build / "meson-logs/clang-tidy-all.md"
        self.entries = []
        for name in ("a.cpp", "b.cpp"):
            path = self.source / name
            path.write_text("int main() { return 0; }\n")
            self.entries.append({"directory": str(self.build), "file": f"../common/{name}",
                                 "arguments": ["c++", "-fno-defer-pop", "-DKEEP=1", f"../common/{name}"]})
        self.save_database()

    def save_database(self):
        (self.build / "compile_commands.json").write_text(json.dumps(self.entries))

    def invoke(self, failure=False, clean=False):
        real_run = subprocess.run
        self.analyzed_files = []

        def tool(command, **kwargs):
            if command[0] == "git":
                return real_run(command, **kwargs)
            if command[-1] == "--version":
                return subprocess.CompletedProcess(command, 0, "LLVM test version\n")
            self.analyzed_files.append(Path(command[1]).name)
            database = Path(command[command.index("-p") + 1]) / "compile_commands.json"
            self.assertNotIn("-fno-defer-pop", database.read_text())
            self.assertIn("-DKEEP=1", database.read_text())
            self.assertIn(f"--config-file={self.root / '.clang-tidy'}", command)
            self.assertNotIn("--fix", command)
            output = (f"{self.source / 'shared.h'}:3:7: warning: use nullptr [modernize-use-nullptr]\n"
                      "    3 | void *p = 0; // ```\n"
                      "      |           ^\n"
                      f"{self.source / 'a.cpp'}:1:1: warning: existing finding [old-check]\n"
                      f"{self.root}/development/subprojects/library.h:1:1: warning: external [check]\n")
            if failure and command[1].endswith("b.cpp"):
                return subprocess.CompletedProcess(command, 1, "error: missing system header\n")
            if clean:
                output = ""
            return subprocess.CompletedProcess(command, 0, output)

        terminal = io.StringIO()
        with patch("shutil.which", return_value="/tools/clang-tidy"), \
                patch("subprocess.run", side_effect=tool), contextlib.redirect_stdout(terminal):
            result = REPORTER["run"](self.args)
        return result, terminal.getvalue()

    def test_reports_advisory_findings_once_with_context_and_log(self):
        result, terminal = self.invoke()
        self.assertEqual(result, 0)
        report = self.report.read_text()
        for output in (terminal, report):
            self.assertEqual(output.count("shared.h:3:7"), 1)
            self.assertIn("void *p = 0", output)
            self.assertNotIn("external", output)
        self.assertIn("Status: Complete", report)
        self.assertIn("processed: 2/2", report)
        self.assertIn("````text", report)
        self.assertIn("external", self.report.with_suffix(".log").read_text())

    def test_tool_failure_preserves_findings_and_marks_incomplete(self):
        result, terminal = self.invoke(failure=True)
        self.assertEqual(result, 1)
        self.assertIn("missing system header", terminal)
        report = self.report.read_text()
        self.assertIn("Status: INCOMPLETE", report)
        self.assertIn("shared.h:3:7", report)
        self.assertIn("missing system header", report)

    def test_clean_run_replaces_previous_findings(self):
        self.invoke()
        result, _ = self.invoke(clean=True)
        self.assertEqual(result, 0)
        report = self.report.read_text()
        self.assertIn("Status: Complete", report)
        self.assertIn("No findings.", report)
        self.assertNotIn("shared.h", report)

    def test_missing_tool_replaces_stale_success_report(self):
        self.invoke()
        with patch("shutil.which", return_value=None), contextlib.redirect_stdout(io.StringIO()):
            result = REPORTER["run"](self.args)
        self.assertEqual(result, 1)
        report = self.report.read_text()
        self.assertIn("Status: INCOMPLETE", report)
        self.assertIn("clang-tidy was not found", report)
        self.assertNotIn("shared.h:3:7", report)

    def test_database_filters_sources_and_preserves_compile_variants(self):
        self.entries.extend([
            {"directory": str(self.build), "file": "../common/a.cpp",
             "command": "c++ '-DOTHER=a b' -fno-reorder-functions ../common/a.cpp"},
            {"directory": str(self.build), "file": "../subprojects/vendor.cpp", "arguments": ["c++"]},
            {"directory": str(self.build), "file": "generated.cpp", "arguments": ["c++"]},
            {"directory": str(self.build), "file": "../../generated/open_perception_kit/bridge.cpp",
             "arguments": ["c++"]},
        ])
        self.save_database()
        destination = self.root / "filtered"
        destination.mkdir()
        files = REPORTER["prepare_database"](self.root, self.build, destination)
        self.assertEqual(len(files), 2)
        entries = json.loads((destination / "compile_commands.json").read_text())
        self.assertEqual(len(entries), 3)
        self.assertIn("-DOTHER=a b", entries[-1]["arguments"])
        self.assertNotIn("-fno-reorder-functions", entries[-1]["arguments"])
        self.assertNotIn("command", entries[-1])

    def test_empty_or_invalid_database_is_not_a_clean_result(self):
        for content in ("[]", "not json", '["invalid entry"]'):
            with self.subTest(content=content):
                (self.build / "compile_commands.json").write_text(content)
                result, _ = self.invoke()
                self.assertEqual(result, 1)
                self.assertIn("INCOMPLETE", self.report.read_text())

    def test_relative_diagnostic_and_compiler_error_without_check(self):
        output = "../common/a.cpp:1:4: fatal error: broken input\n  1 | broken\n"
        diagnostics = list(REPORTER["parse_diagnostics"](output, self.build, self.root, self.build))
        self.assertEqual(len(diagnostics), 1)
        key, check, detail = diagnostics[0]
        self.assertEqual(key[:4], ("development/common/a.cpp", 1, 4, "fatal error"))
        self.assertEqual(check, "compiler")
        self.assertIn("1 | broken", detail)

    def git(self, *arguments):
        return subprocess.run(["git", "-C", str(self.root), *arguments],
                              capture_output=True, text=True, check=True).stdout.strip()

    def init_git(self):
        self.git("init", "--initial-branch=develop")
        self.git("config", "user.name", "Clang Tidy Test")
        self.git("config", "user.email", "clang-tidy-test@example.invalid")
        self.git("config", "commit.gpgsign", "false")
        (self.root / ".gitignore").write_text("development/build/\n")
        self.git("add", ".")
        self.git("commit", "-m", "Baseline")
        baseline = self.git("rev-parse", "HEAD")
        self.git("checkout", "-b", "feature")
        self.args.scope = "new"
        self.args.base = "develop"
        return baseline

    def changes(self, base="develop"):
        changes, comparison, _ = REPORTER["changed_lines"](self.root, self.build, base)
        return changes, comparison

    def test_source_only_changes_do_not_analyze_unchanged_files(self):
        self.init_git()
        (self.source / "a.cpp").write_text("int main() { return 1; }\n")
        # The fake tool would fail for b.cpp if the runner incorrectly analyzed it.
        result, terminal = self.invoke(failure=True)
        self.assertEqual(result, 0)
        self.assertEqual(self.analyzed_files, ["a.cpp"])
        report = (self.build / "meson-logs/clang-tidy-new.md").read_text()
        for output in (terminal, report):
            self.assertIn("Source-only changes: selected 1 of 2", output)
            self.assertIn("a.cpp:1:1", output)
            self.assertNotIn("shared.h:3:7", output)
        self.assertIn("processed: 1/1", report)

    def test_deleted_or_renamed_header_forces_full_scan_when_source_changes(self):
        header = self.source / "shared.h"
        header.write_text("// header\n")
        self.init_git()
        (self.source / "a.cpp").write_text("int main() { return 1; }\n")
        renamed = self.source / "renamed.h"
        self.git("mv", str(header), str(renamed))
        result, terminal = self.invoke()
        self.assertEqual(result, 0)
        self.assertCountEqual(self.analyzed_files, ["a.cpp", "b.cpp"])
        self.assertIn("Header changes detected", terminal)
        self.git("mv", str(renamed), str(header))
        header.unlink()
        result, terminal = self.invoke(failure=True)
        self.assertEqual(result, 1)
        self.assertCountEqual(self.analyzed_files, ["a.cpp", "b.cpp"])
        self.assertIn("Header changes detected", terminal)

    def test_source_outside_database_is_explicitly_skipped(self):
        self.init_git()
        new_source = self.source / "disabled.cpp"
        new_source.write_text("int disabled() { return 0; }\n")
        self.git("add", str(new_source))
        result, terminal = self.invoke()
        self.assertEqual(result, 0)
        self.assertEqual(self.analyzed_files, [])
        self.assertIn("selected 0 of 2", terminal)
        self.assertIn("outside the active compilation database", terminal)

    def test_changed_lines_include_branch_staged_and_unstaged_edits(self):
        path = self.source / "a.cpp"
        path.write_text("one\ntwo\nthree\nfour\nfive\nsix\n")
        baseline = self.init_git()
        path.write_text("one\nbranch change\nthree\nfour\nfive\nsix\n")
        self.git("add", ".")
        self.git("commit", "-m", "Feature edit")
        # Advance the target branch: its new lines must not enter the feature's comparison.
        self.git("checkout", "develop")
        (self.source / "b.cpp").write_text("target branch only\n")
        self.git("add", ".")
        self.git("commit", "-m", "Target edit")
        self.git("checkout", "feature")
        path.write_text("one\nbranch change\nthree\nstaged change\nfive\nsix\n")
        self.git("add", ".")
        path.write_text("one\nbranch change\nthree\nstaged change\nfive\nunstaged change\n")
        self.git("config", "diff.interHunkContext", "10")
        changes, comparison = self.changes()
        self.assertEqual(changes, {"development/common/a.cpp": [(2, 2), (4, 4), (6, 6)]})
        self.assertIn(f"Merge base: {baseline}", comparison)
        self.assertNotEqual(self.git("rev-parse", "develop"), baseline)

    def test_renames_additions_and_deletions_use_current_line_numbers(self):
        original = self.source / "original.h"
        original.write_text("\n".join(f"// line {i}" for i in range(20)) + "\n")
        self.init_git()
        # Exercise Git-quoted names, literal pathspec characters and non-ASCII paths.
        renamed = self.source / 'renamed [é] "header".h'
        self.git("mv", str(original), str(renamed))
        self.assertEqual(self.changes()[0], {})
        renamed.write_text(renamed.read_text().replace("// line 10", "// modified line"))
        added = self.source / "added.cpp"
        added.write_text("line one\nline two\n")
        untracked = self.source / "untracked.cpp"
        untracked.write_text("not in scope\n")
        self.git("add", "-N", str(added))
        (self.source / "a.cpp").unlink()
        changes, _ = self.changes()
        self.assertEqual(changes, {
            renamed.relative_to(self.root).as_posix(): [(11, 11)],
            "development/common/added.cpp": [(1, 2)],
        })

    def test_deletion_only_and_non_cpp_changes_have_no_new_lines(self):
        path = self.source / "a.cpp"
        path.write_text("one\ntwo\nthree\n")
        self.init_git()
        path.write_text("one\nthree\n")
        (self.root / ".clang-tidy").write_text("Checks: '-*'\n")
        self.assertEqual(self.changes()[0], {})
        result, terminal = self.invoke()
        self.assertEqual(result, 0)
        self.assertIn("skipping analysis", terminal)
        self.assertEqual(self.analyzed_files, [])
        report = (self.build / "meson-logs/clang-tidy-new.md").read_text()
        self.assertIn("Status: Complete", report)
        self.assertIn("processed: 0/0", report)

    def test_reference_fallbacks_and_missing_ref(self):
        baseline = self.init_git()
        self.git("config", "branch.feature.vscode-merge-base", "develop")
        self.assertIn("Reference: develop", self.changes(base=None)[1])
        self.git("config", "--unset", "branch.feature.vscode-merge-base")
        self.git("update-ref", "refs/remotes/origin/develop", baseline)
        self.git("symbolic-ref", "refs/remotes/origin/HEAD", "refs/remotes/origin/develop")
        self.assertIn("Reference: refs/remotes/origin/develop", self.changes(base=None)[1])
        self.assertIn("Reference: refs/remotes/origin/develop", self.changes(base="develop")[1])
        self.args.base = "missing-branch"
        result, _ = self.invoke()
        self.assertEqual(result, 1)
        report = (self.build / "meson-logs/clang-tidy-new.md").read_text()
        self.assertIn("INCOMPLETE", report)
        self.assertIn("Cannot resolve", report)

    def test_unrelated_or_unconfigured_base_fails_clearly(self):
        self.init_git()
        with self.assertRaisesRegex(ValueError, "No comparison reference"):
            self.changes(base=None)
        self.git("checkout", "--orphan", "unrelated")
        self.git("add", ".")
        self.git("commit", "-m", "Unrelated root")
        self.git("checkout", "feature")
        with self.assertRaisesRegex(ValueError, "No merge base"):
            self.changes(base="unrelated")

    def test_new_report_filters_locations_and_preserves_overall_report(self):
        header = self.source / "shared.h"
        header.write_text("one\ntwo\nold line\nfour\n")
        self.invoke()
        overall = self.report.read_text()
        self.init_git()
        header.write_text("one\ntwo\nnew line\nfour\n")
        result, terminal = self.invoke()
        self.assertEqual(result, 0)
        self.assertCountEqual(self.analyzed_files, ["a.cpp", "b.cpp"])
        report = (self.build / "meson-logs/clang-tidy-new.md").read_text()
        for output in (terminal, report):
            self.assertEqual(output.count("shared.h:3:7"), 1)
            self.assertNotIn("existing finding", output)
            self.assertIn("Merge base:", output)
        self.assertEqual(self.report.read_text(), overall)
        # Errors on unchanged lines must still fail the run and remain visible.
        result, terminal = self.invoke(failure=True)
        self.assertEqual(result, 1)
        self.assertIn("missing system header", terminal)
        self.assertIn("INCOMPLETE", (self.build / "meson-logs/clang-tidy-new.md").read_text())

    @unittest.skipUnless(all(shutil.which(tool) for tool in ("meson", "ninja", "clang-tidy", "c++")),
                         "Meson, Ninja, clang-tidy, and a C++ compiler are required")
    def test_real_meson_target_reports_header_warning_and_analysis_failure(self):
        # Use the production Meson wiring in a small project, without OPK runtime dependencies.
        shutil.copytree(REPOSITORY / "development/quality", self.root / "development/quality")
        helper = self.root / "scripts/private/clang-tidy-report.py"
        helper.parent.mkdir(parents=True)
        shutil.copy2(HELPER, helper)
        (self.root / "development/meson.build").write_text(
            "project('tidy-fixture', 'cpp')\n"
            "opk_repository_root = meson.project_source_root() / '..'\n"
            "configure_file(output: 'configured.h', configuration: {'VALUE': 0})\n"
            "executable('fixture', ['common/a.cpp', 'common/b.cpp'], include_directories: include_directories('.'))\n"
            "subdir('quality')\n")
        (self.source / "shared.h").write_text("inline void *pointer() { return 0; }\n")
        (self.source /
         "a.cpp").write_text('#include "shared.h"\n#include "configured.h"\nint main() { return VALUE; }\n')
        (self.source / "b.cpp").write_text('#include "shared.h"\nvoid *other() { return pointer(); }\n')
        env = {**os.environ, "CLANG_TIDY": shutil.which("clang-tidy"), "CLANG_TIDY_JOBS": "2"}
        for command in (
            ["meson", "setup", str(self.build), str(self.root / "development")],
            ["ninja", "-C", str(self.build), "clang-tidy-all"],
        ):
            result = subprocess.run(command, capture_output=True, text=True, env=env, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("modernize-use-nullptr", result.stdout)
        self.assertEqual(self.report.read_text().count("shared.h:1:"), 1, self.report.read_text())
        self.assertIn("Status: Complete", self.report.read_text())
        self.init_git()
        # Source-only edits must run exactly one translation unit with the real tool.
        with (self.source / "a.cpp").open("a") as source:
            source.write("void *new_source_pointer() { return 0; }\n")
        env["CLANG_TIDY_BASE"] = "develop"
        result = subprocess.run(["ninja", "-C", str(self.build), "clang-tidy-new"],
                                capture_output=True, text=True, env=env, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        new_report = (self.build / "meson-logs/clang-tidy-new.md").read_text()
        self.assertIn("processed: 1/1", new_report)
        self.assertIn("a.cpp:4:", new_report)
        self.assertNotIn("shared.h:1:", new_report)
        # Header edits must analyze both including TUs but omit the unchanged warning on line 1.
        with (self.source / "shared.h").open("a") as header:
            header.write("inline void *new_pointer() { return 0; }\n")
        env["CLANG_TIDY_BASE"] = "develop"
        result = subprocess.run(["ninja", "-C", str(self.build), "clang-tidy-new"],
                                capture_output=True, text=True, env=env, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        new_report = (self.build / "meson-logs/clang-tidy-new.md").read_text()
        self.assertIn("processed: 2/2", new_report)
        self.assertEqual(new_report.count("shared.h:2:"), 1, new_report)
        self.assertNotIn("shared.h:1:", new_report)
        self.assertNotIn("shared.h:1:", result.stdout)
        # A second invocation must replace the report even when analysis fails.
        (self.source / "b.cpp").write_text('#include "does-not-exist.h"\n')
        result = subprocess.run(["meson", "compile", "-C", str(self.build), "clang-tidy-all"],
                                capture_output=True, text=True, env=env, timeout=90)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Status: INCOMPLETE", self.report.read_text())
        self.assertIn("does-not-exist.h", self.report.read_text())


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
