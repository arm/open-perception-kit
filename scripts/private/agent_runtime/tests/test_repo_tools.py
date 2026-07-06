################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import sys
from pathlib import Path
import unittest
import tempfile
import textwrap

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tests'))
from agent_workflow_test_support import (  # noqa: E402
    OPENAI_AGENT_REPO_TOOLS_SCRIPT,
    OPENAI_AGENT_RUNTIME_CONTEXT,
    load_agent_workflow_module_with_fake_sdk,
)


class AgentRuntimeRepoToolTests(unittest.TestCase):
    def test_openai_agent_runner_executes_simple_commands_without_shell_expansion(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_commands",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(Path(temp_dir), 10)
            output = repo_tools.run_shell_command('echo "$(git push)" && git diff --check')

        self.assertIn("$ echo '$(git push)'", output)
        self.assertIn("$(git push)", output)
        self.assertIn("$ git diff --check", output)

    def test_openai_agent_runner_executes_tokenized_pipelines_and_redirection(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_pipeline_commands",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(repo_root, 10)
            output = repo_tools.run_shell_command(
                "printf 'one\\ntwo\\n' | sed -n 2p > out.txt; cat < out.txt"
            )

            written_output = (repo_root / "out.txt").read_text(encoding="utf-8")

        self.assertEqual(written_output, "two\n")
        self.assertIn("$ printf 'one\\ntwo\\n' | sed -n 2p > out.txt", output)
        self.assertIn("$ cat < out.txt", output)
        self.assertIn("two", output)

    def test_openai_agent_runner_feeds_pipeline_stdout_to_next_stage_stdin(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_pipeline_stdin",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(Path(temp_dir), 10)
            output = repo_tools.run_shell_command(
                "printf 'alpha\\nbeta\\n' | head -n 1; printf 'alpha\\nbeta\\n' | grep beta"
            )

        self.assertIn("$ printf 'alpha\\nbeta\\n' | head -n 1", output)
        self.assertIn("$ printf 'alpha\\nbeta\\n' | grep beta", output)
        self.assertIn("--- stdout ---\nalpha\n", output)
        self.assertIn("--- stdout ---\nbeta\n", output)

    def test_openai_agent_runner_merges_stderr_into_stdout(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_stderr_redirection",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(Path(temp_dir), 10)
            output = repo_tools.run_shell_command("ls missing-workflow-agent-file 2>&1")

        self.assertIn("$ ls missing-workflow-agent-file 2>&1", output)
        self.assertIn("--- stdout ---", output)
        self.assertIn("missing-workflow-agent-file", output)
        self.assertIn("--- stderr ---\n", output)

    def test_openai_agent_runner_treats_trailing_two_as_argument(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_trailing_two_argument",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(Path(temp_dir), 10)
            output = repo_tools.run_shell_command("echo 2")

        self.assertIn("$ echo 2", output)
        self.assertIn("--- stdout ---\n2", output)

    def test_openai_agent_runner_rejects_pipeline_stderr_redirection(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_pipeline_stderr_redirection",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(Path(temp_dir), 10)
            with self.assertRaisesRegex(ValueError, "stderr redirection with pipelines"):
                repo_tools.run_shell_command("printf ok 2>&1 | cat")
            with self.assertRaisesRegex(ValueError, "stderr redirection with pipelines"):
                repo_tools.run_shell_command("printf ok | cat 2>&1")

    def test_openai_agent_runner_rejects_redirection_outside_repo(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_redirection_guard",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(Path(temp_dir) / "repo", 10)
            with self.assertRaisesRegex(ValueError, "escapes repository root"):
                repo_tools.run_shell_command("printf bad > ../outside.txt")

    def test_openai_agent_runner_rejects_git_metadata_reads(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_metadata_read_guard",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            git_dir = repo_root / ".git"
            git_dir.mkdir()
            (git_dir / "config").write_text("credential = unsafe\n", encoding="utf-8")
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(repo_root, 10)

            with self.assertRaisesRegex(ValueError, "Read path targets git metadata"):
                repo_tools.read_repo_file(".git/config")
            with self.assertRaisesRegex(ValueError, "Command argument targets git metadata"):
                repo_tools.run_shell_command("cat .git/config")
            with self.assertRaisesRegex(ValueError, "Shell redirection path targets git metadata"):
                repo_tools.run_shell_command("cat < .git/config")

    def test_openai_agent_runner_applies_safe_unified_diff(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_patch_guard_safe",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            target = repo_root / "example.txt"
            target.write_text("old\n", encoding="utf-8")
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(repo_root, 10)

            output = repo_tools.apply_unified_diff(
                textwrap.dedent(
                    """\
                    diff --git a/example.txt b/example.txt
                    --- a/example.txt
                    +++ b/example.txt
                    @@ -1 +1 @@
                    -old
                    +new
                    """
                )
            )

            self.assertIn("exit_code=0", output)
            self.assertEqual(target.read_text(encoding="utf-8"), "new\n")

    def test_openai_agent_runner_rejects_patch_paths_outside_safe_tree(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_patch_guard_reject",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir) / "repo"
            repo_root.mkdir()
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(repo_root, 10)

            git_metadata_patch = textwrap.dedent(
                """\
                diff --git a/.git/config b/.git/config
                --- a/.git/config
                +++ b/.git/config
                @@ -0,0 +1 @@
                +unsafe
                """
            )
            with self.assertRaisesRegex(ValueError, "Patch path targets git metadata"):
                repo_tools.apply_unified_diff(git_metadata_patch)

            escaping_patch = textwrap.dedent(
                """\
                diff --git a/../outside.txt b/../outside.txt
                --- a/../outside.txt
                +++ b/../outside.txt
                @@ -0,0 +1 @@
                +unsafe
                """
            )
            with self.assertRaisesRegex(ValueError, "escapes repository root"):
                repo_tools.apply_unified_diff(escaping_patch)

    def test_openai_agent_runner_blocks_mutating_git_commands_after_shell_splitting(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_git_guards",
        )

        repo_tools.reject_unsafe_shell_command('echo "git push" && git diff --check')
        for command in (
            "git diff --check",
            "git show HEAD",
            "git log --oneline -1",
            "git status --short",
            "git ls-tree HEAD",
            "git grep agent-review",
            "git rev-parse HEAD",
            "git merge-base HEAD origin/main",
            "git cat-file -t HEAD",
            "git apply --check /tmp/example.patch",
            "git diff --check | sed -n 1,20p",
        ):
            repo_tools.reject_unsafe_shell_command(command)
        with self.assertRaisesRegex(ValueError, "git push"):
            repo_tools.reject_unsafe_shell_command('echo ok && git push')
        with self.assertRaisesRegex(ValueError, "git push"):
            repo_tools.reject_unsafe_shell_command("echo ok | git push")
        with self.assertRaisesRegex(ValueError, "Unsupported shell syntax"):
            repo_tools.reject_unsafe_shell_command("git diff --check || true")
        with self.assertRaisesRegex(ValueError, "Unsupported empty command"):
            repo_tools.reject_unsafe_shell_command("git diff --check |")
        with self.assertRaisesRegex(ValueError, "redirection requires a command"):
            repo_tools.reject_unsafe_shell_command("> out.txt")
        for command in (
            "git apply /tmp/example.patch",
            "git add -A",
            "git clean -fd",
            "git branch -D stale-branch",
            "git remote set-url origin https://example.invalid/repo.git",
            "git restore .",
            "git tag -d v0.0.0",
            "git config alias.publish push",
            "git diff --output=/tmp/diff.patch",
        ):
            with self.assertRaisesRegex(ValueError, "Command is intentionally blocked"):
                repo_tools.reject_unsafe_shell_command(command)


if __name__ == "__main__":
    unittest.main()
