################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest


def load_agent_static_analysis_module():
    module_path = Path(__file__).resolve().parents[1] / "opk_ci" / "agent_static_analysis.py"
    spec = importlib.util.spec_from_file_location("agent_static_analysis_under_test", module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {module_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


agent_static_analysis = load_agent_static_analysis_module()


class AgentStaticAnalysisTests(unittest.TestCase):
    def test_derives_removed_paths_from_name_status(self):
        name_status = "\0".join(
            [
                "R100",
                "tools/retired-runner.py",
                "tools/agent-runner.py",
                "D",
                "scripts/private/unused_helper.py",
                "",
            ]
        )

        self.assertEqual(
            agent_static_analysis.parse_removed_or_renamed_paths(name_status),
            ["tools/retired-runner.py", "scripts/private/unused_helper.py"],
        )

    def test_reference_tokens_include_suffixless_path(self):
        self.assertEqual(
            agent_static_analysis.reference_tokens_for_removed_path("scripts/private/old-helper.py"),
            {"scripts/private/old-helper.py", "scripts/private/old-helper"},
        )

    def test_reference_tokens_do_not_alias_existing_suffixless_path(self):
        removed_path = "Dockerfile" + ".dev"

        self.assertEqual(
            agent_static_analysis.reference_tokens_for_removed_path(
                removed_path,
                repo_root=Path(__file__).resolve().parents[3],
            ),
            {removed_path},
        )

    def test_reference_tokens_keep_non_executable_suffix(self):
        removed_path = "devices" + ".env"

        self.assertEqual(
            agent_static_analysis.reference_tokens_for_removed_path(removed_path),
            {removed_path},
        )

    def test_reference_matching_requires_path_boundaries(self):
        removed_path = "scripts/playwright/pages/" + "publish"

        self.assertTrue(
            agent_static_analysis.line_references_path(
                f"python3 {removed_path} --help",
                removed_path,
            )
        )
        for prefix in (
            "./",
            "${GITHUB_WORKSPACE}/",
            "$GITHUB_WORKSPACE/",
            ".agent-runtime/agent-stabilization-helper/",
        ):
            self.assertTrue(
                agent_static_analysis.line_references_path(
                    f"python3 {prefix}{removed_path} --help",
                    removed_path,
                )
            )
        self.assertFalse(
            agent_static_analysis.line_references_path(
                "python3 scripts/playwright/pages/publish_playwright_pages.py",
                removed_path,
            )
        )

    def test_reference_matching_allows_generated_path_marker(self):
        generated_path = "devices" + ".env"

        self.assertFalse(
            agent_static_analysis.line_references_path(
                'DEV_ENV_FILE="devices.env" # agent-static-analysis: allow-generated-path',
                generated_path,
            )
        )

    def test_path_pattern_marker_is_explicit_and_token_scoped(self):
        pattern = ".git" + "modules"
        marker = f"# agent-static-analysis: allow-path-pattern={pattern} (generic classifier)"
        line = f'filename in {{"{pattern}", "old-helper.py"}} {marker}'

        self.assertFalse(agent_static_analysis.line_references_path(line, pattern))
        self.assertTrue(agent_static_analysis.line_references_path(line, "old-helper.py"))
        for invalid_marker in (
            "# agent-static-analysis: allow-path-pattern",
            f"# agent-static-analysis: allow-path-pattern={pattern}.other",
            f"agent-static-analysis: allow-path-pattern={pattern}",
        ):
            with self.subTest(marker=invalid_marker):
                self.assertTrue(
                    agent_static_analysis.line_references_path(
                        f'filename == "{pattern}" {invalid_marker}', pattern
                    )
                )

    def test_unmarked_basename_references_are_still_checked(self):
        removed_path = ".git" + "modules"
        for line in (
            f'filename == "{removed_path}"',
            f'git config --file "${{EXTERNAL_DIR}}/{removed_path}"',
        ):
            self.assertTrue(agent_static_analysis.line_references_path(line, removed_path))

    def test_ignore_rules_are_not_source_references(self):
        self.assertNotIn(".gitignore", agent_static_analysis.AGENT_STATIC_REFERENCE_PATHS)


class RemovedReferenceGitTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.repo_root = Path(directory.name)
        self.git("init", "--template=", "-b", "main")
        (self.repo_root / ".github").mkdir()
        (self.repo_root / ".github/CI-README.md").write_text("References\n", encoding="utf-8")
        self.git("add", ".")
        self.commit()

    def git(self, *args):
        return subprocess.run(
            ["git", *args], cwd=self.repo_root, check=True, capture_output=True, text=True
        ).stdout.strip()

    def commit(self):
        self.git(
            "-c", "user.name=Static Analysis Test", "-c", "user.email=static@example.test",
            "commit", "-m", "Test fixture",
        )

    def prepare_removed_path(self, path, *, gitlink=False):
        if gitlink:
            self.git("update-index", "--add", "--cacheinfo", f"160000,{self.git('rev-parse', 'HEAD')},{path}")
        else:
            source = self.repo_root / path
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_text("original source\n", encoding="utf-8")
            self.git("add", path)
        (self.repo_root / ".github/CI-README.md").write_text(f"Use `{path}`\n", encoding="utf-8")
        self.git("add", ".github/CI-README.md")
        self.commit()
        self.git("branch", "base")
        self.git("rm", "--cached", path)
        if not gitlink:
            (self.repo_root / path).unlink()

    def assert_removed_paths(self, expected):
        for staged in (True, False):
            with self.subTest(staged=staged):
                if not staged:
                    self.commit()
                removed = agent_static_analysis.removed_or_renamed_paths(
                    self.repo_root, base_ref="" if staged else "base", staged=staged
                )
                self.assertEqual(removed, expected)
                self.assertEqual(
                    agent_static_analysis.find_removed_reference_violations(self.repo_root, removed),
                    [f".github/CI-README.md:1: removed path reference '{path}'" for path in expected],
                )

    def test_removed_gitlink_replaced_by_tracked_directory(self):
        path = "tools/vendor-sdk"
        self.prepare_removed_path(path, gitlink=True)
        replacement = self.repo_root / path / "nested/source.py"
        replacement.parent.mkdir(parents=True)
        replacement.write_text("vendored source\n", encoding="utf-8")
        self.git("add", path)
        self.assert_removed_paths([])

    def test_removed_file_replaced_by_tracked_directory(self):
        path = "tools/vendor-sdk"
        self.prepare_removed_path(path)
        replacement = self.repo_root / path / "source.py"
        replacement.parent.mkdir(parents=True)
        replacement.write_text("replacement source\n", encoding="utf-8")
        self.git("add", path)
        self.assert_removed_paths([])

    def test_untracked_directory_does_not_hide_removed_gitlink(self):
        path = "tools/vendor-sdk"
        self.prepare_removed_path(path, gitlink=True)
        leftover = self.repo_root / path / "source.py"
        leftover.parent.mkdir(parents=True)
        leftover.write_text("untracked leftover\n", encoding="utf-8")
        self.assert_removed_paths([path])

    def test_sibling_directory_does_not_hide_removed_path(self):
        path = "tools/vendor-sdk"
        self.prepare_removed_path(path, gitlink=True)
        sibling = self.repo_root / f"{path}-other/source.py"
        sibling.parent.mkdir(parents=True)
        sibling.write_text("unrelated source\n", encoding="utf-8")
        self.git("add", f"{path}-other")
        self.assert_removed_paths([path])

    def test_deleted_basename_with_untracked_leftover_is_reported(self):
        path = ".git" + "modules"
        self.prepare_removed_path(path)
        (self.repo_root / path).write_text("untracked leftover\n", encoding="utf-8")
        self.assert_removed_paths([path])

    def test_renamed_source_reference_is_reported(self):
        path = "scripts/private/old-helper.py"
        self.prepare_removed_path(path)
        replacement = self.repo_root / "scripts/private/new-helper.py"
        replacement.write_text("original source\n", encoding="utf-8")
        self.git("add", "scripts/private/new-helper.py")
        self.assert_removed_paths([path])

    def test_base_ref_uses_head_not_staged_replacement(self):
        path = "tools/vendor-sdk"
        self.prepare_removed_path(path, gitlink=True)
        self.commit()
        replacement = self.repo_root / path / "source.py"
        replacement.parent.mkdir(parents=True)
        replacement.write_text("staged source\n", encoding="utf-8")
        self.git("add", path)
        self.assertEqual(
            agent_static_analysis.removed_or_renamed_paths(self.repo_root, base_ref="base", staged=True),
            [path],
        )

    def test_markers_do_not_suppress_other_lines_or_paths(self):
        path = ".git" + "modules"
        self.prepare_removed_path(path)
        references = self.repo_root / ".github/CI-README.md"
        references.write_text(
            f'"{path}" # agent-static-analysis: allow-path-pattern={path}\n'
            f'"${{EXTERNAL_DIR}}/{path}" # agent-static-analysis: allow-generated-path\n'
            f'"{path}"\n'
            f'"{path}" # agent-static-analysis: allow-path-pattern=other\n',
            encoding="utf-8",
        )
        self.assertEqual(
            agent_static_analysis.find_removed_reference_violations(self.repo_root, [path]),
            [f".github/CI-README.md:{line}: removed path reference '{path}'" for line in (3, 4)],
        )


if __name__ == "__main__":
    unittest.main()
