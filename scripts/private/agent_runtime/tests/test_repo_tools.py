################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from contextlib import redirect_stderr
import io
import os
import subprocess
import sys
from pathlib import Path
import unittest
import tempfile
import textwrap
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from test_support.agent_workflow import (  # noqa: E402
    AGENT_REVIEW_CONTEXT,
    OPENAI_AGENT_REPO_TOOLS_SCRIPT,
    OPENAI_AGENT_RUNTIME_CONTEXT,
    OPENAI_AGENT_SHELL_TOOLS_SCRIPT,
    load_agent_workflow_module_with_fake_sdk,
)


class AgentRuntimeRepoToolTests(unittest.TestCase):
    @staticmethod
    def activate_review_context(repo_root: Path):
        return OPENAI_AGENT_RUNTIME_CONTEXT.activate_run_context(
            AGENT_REVIEW_CONTEXT.ReviewRunContext(
                repo_root=repo_root,
                command_timeout=10,
                repository="Arm-Debug/amp-dev-forge",
                base_ref="origin/develop",
                head_ref="HEAD",
                base_sha="a" * 40,
                head_sha="b" * 40,
                pull_request=AGENT_REVIEW_CONTEXT.PullRequestEvidence(
                    number=101,
                    title="Safe title",
                    body=None,
                    url=None,
                ),
                limits=AGENT_REVIEW_CONTEXT.ReviewLimits(
                    max_review_files=120,
                    max_review_changed_lines=15000,
                    max_pr_title_chars=AGENT_REVIEW_CONTEXT.MAX_PR_TITLE_CHARS,
                    max_pr_body_chars=AGENT_REVIEW_CONTEXT.MAX_PR_BODY_CHARS,
                    max_pr_url_chars=AGENT_REVIEW_CONTEXT.MAX_PR_URL_CHARS,
                ),
                completeness=AGENT_REVIEW_CONTEXT.ReviewCompleteness(
                    pull_request_available=True,
                    pr_title_truncated=False,
                    pr_body_original_chars=0,
                    pr_body_normalized_chars=0,
                    pr_body_truncated=False,
                    pr_url_truncated=False,
                ),
            )
        )

    def test_openai_agent_runner_scrubs_credentials_and_github_metadata_from_commands(self):
        shell_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_SHELL_TOOLS_SCRIPT,
            "agent_runtime.tools.shell_fake_sdk_scrubbed_environment",
        )
        with tempfile.TemporaryDirectory() as temp_dir, mock.patch.dict(
            os.environ,
            {
                "OPENAI_API_KEY": "openai-test-value",  # pragma: allowlist secret
                "OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS": "proxy-test-value",  # pragma: allowlist secret
                "GITHUB_TOKEN": "github-test-value",  # pragma: allowlist secret
                "GITHUB_HEAD_REF": "untrusted-pr-head",
                "ARTIFACTORY_KEY": "artifactory-test-value",  # pragma: allowlist secret
                "DOCKER_AUTH_CONFIG": "docker-test-value",  # pragma: allowlist secret
                "CI_JOB_JWT": "jwt-test-value",  # pragma: allowlist secret
                "CMAKE_AUTH_CONFIG": "prefixed-auth-test-value",  # pragma: allowlist secret
                "PEK_ONNXRUNTIME_ROOT": "/opt/onnxruntime",
                "PEK_API_KEY": "pek-key-test-value",  # pragma: allowlist secret
                "UNRELATED_RUNNER_VALUE": "not-required-by-builds",
                "CMAKE_GENERATOR": "toolchain-preserved-marker",
                "HOME": "credential-home-marker",
            },
        ):
            self.activate_review_context(Path(temp_dir))
            context = OPENAI_AGENT_RUNTIME_CONTEXT.require_run_context()
            environment = shell_tools.build_subprocess_environment(context)

            self.assertIn("PATH", environment)
            self.assertEqual(environment["CMAKE_GENERATOR"], "toolchain-preserved-marker")
            self.assertEqual(environment["PEK_ONNXRUNTIME_ROOT"], "/opt/onnxruntime")
            self.assertEqual(
                environment["HOME"],
                str(context.repo_root / ".agent-runtime/review-shell-home"),
            )
            self.assertNotIn("credential-home-marker", environment.values())
            self.assertNotIn("openai-test-value", environment.values())
            self.assertNotIn("proxy-test-value", environment.values())
            self.assertNotIn("github-test-value", environment.values())
            self.assertNotIn("artifactory-test-value", environment.values())
            self.assertNotIn("docker-test-value", environment.values())
            self.assertNotIn("jwt-test-value", environment.values())
            self.assertNotIn("prefixed-auth-test-value", environment.values())
            self.assertNotIn("pek-key-test-value", environment.values())
            self.assertNotIn("not-required-by-builds", environment.values())
            self.assertNotIn("untrusted-pr-head", environment.values())
            self.assertNotIn("OPENAI_API_KEY", environment)
            self.assertNotIn("GITHUB_HEAD_REF", environment)
            self.assertNotIn("ARTIFACTORY_KEY", environment)
            self.assertNotIn("DOCKER_AUTH_CONFIG", environment)
            self.assertNotIn("CI_JOB_JWT", environment)
            self.assertNotIn("CMAKE_AUTH_CONFIG", environment)
            self.assertNotIn("PEK_API_KEY", environment)
            self.assertNotIn("UNRELATED_RUNNER_VALUE", environment)

    def test_review_agent_preserves_documented_validation_command_surface(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_review_validation_commands",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            (repo_root / "valid.py").write_text("VALUE = 1\n", encoding="utf-8")
            self.activate_review_context(repo_root)

            output = repo_tools.run_shell_command(
                "find . -name '*.py' -print0 | xargs -0 python3 -m py_compile"
            )

        self.assertIn("$ find . -name '*.py' -print0 | xargs -0 python3 -m py_compile", output)

    def test_non_review_agent_runner_preserves_existing_process_environment(self):
        shell_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_SHELL_TOOLS_SCRIPT,
            "agent_runtime.tools.shell_fake_sdk_preserved_environment",
        )
        with tempfile.TemporaryDirectory() as temp_dir, mock.patch.dict(
            os.environ,
            {"CUSTOM_TOOLCHAIN_VARIABLE": "preserved-for-edit-agents"},
        ):
            context = OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(Path(temp_dir), 10)
            environment = shell_tools.build_subprocess_environment(context)

        self.assertEqual(environment["CUSTOM_TOOLCHAIN_VARIABLE"], "preserved-for-edit-agents")

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

    def test_openai_agent_repo_tools_write_diagnostics_to_stderr(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_diagnostics",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(Path(temp_dir), 10)
            stderr = io.StringIO()
            with redirect_stderr(stderr):
                output = repo_tools.run_shell_command("echo ok")

        self.assertIn("ok", output)
        self.assertIn("agent-diagnostic", stderr.getvalue())
        self.assertIn("tool=run_shell_command", stderr.getvalue())

    def test_openai_agent_runner_executes_tokenized_pipelines_and_stdin_redirection(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_pipeline_commands",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            (repo_root / "input.txt").write_text("one\ntwo\n", encoding="utf-8")
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(repo_root, 10)
            output = repo_tools.run_shell_command("cat < input.txt | sed -n 2p")

            self.assertFalse((repo_root / "out.txt").exists())

        self.assertIn("$ cat < input.txt | sed -n 2p", output)
        self.assertIn("--- stdout ---\ntwo\n", output)

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
            repo_root = Path(temp_dir) / "repo"
            repo_root.mkdir()
            (Path(temp_dir) / "outside.txt").write_text("outside\n", encoding="utf-8")
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(repo_root, 10)
            with self.assertRaisesRegex(ValueError, "escapes repository root"):
                repo_tools.run_shell_command("cat < ../outside.txt")

    def test_openai_agent_runner_rejects_stdout_redirection(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_stdout_redirection_guard",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(Path(temp_dir), 10)
            with self.assertRaisesRegex(ValueError, "stdout redirection is not supported"):
                repo_tools.run_shell_command("printf bad > out.txt")
            with self.assertRaisesRegex(ValueError, "stdout redirection is not supported"):
                repo_tools.run_shell_command("printf bad >> out.txt")

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
            with self.assertRaisesRegex(ValueError, "Shell stdin redirection path targets git metadata"):
                repo_tools.run_shell_command("cat < .git/config")

    def test_openai_agent_runner_omits_symlinked_git_metadata_from_file_listing(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_metadata_list_guard",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir) / "repo"
            repo_root.mkdir()
            git_dir = repo_root / ".git"
            git_dir.mkdir()
            (git_dir / "config").write_text("credential = unsafe\n", encoding="utf-8")
            (repo_root / "safe.txt").write_text("safe\n", encoding="utf-8")
            (repo_root / "safe-link.txt").symlink_to(repo_root / "safe.txt")
            (repo_root / "metadata-link").symlink_to(git_dir / "config")
            nested_dir = repo_root / "nested"
            nested_dir.mkdir()
            (nested_dir / "metadata-link").symlink_to(git_dir / "config")
            outside_file = Path(temp_dir) / "outside.txt"
            outside_file.write_text("outside\n", encoding="utf-8")
            (repo_root / "outside-link").symlink_to(outside_file)
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(repo_root, 10)

            output = repo_tools.list_repo_files()

        self.assertIn("safe.txt", output)
        self.assertIn("safe-link.txt", output)
        self.assertNotIn("metadata-link", output)
        self.assertNotIn("outside-link", output)
        self.assertNotIn(".git/config", output)

    def test_review_repo_tools_hide_runtime_outputs_except_packet(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_review_hidden_paths",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            (repo_root / "src.py").write_text("VALUE = 1\n", encoding="utf-8")
            venv_file = repo_root / ".agent-runtime" / "openai-agent-venv" / "lib" / "sdk.py"
            venv_file.parent.mkdir(parents=True)
            venv_file.write_text("sdk internals\n", encoding="utf-8")
            review_output = repo_root / ".github" / "agent-runtime" / "review" / "out" / "review.json"
            review_output.parent.mkdir(parents=True)
            review_output.write_text("{}\n", encoding="utf-8")
            packet_index = review_output.parent / "review-packet" / "index.md"
            packet_index.parent.mkdir(parents=True)
            packet_index.write_text("packet\n", encoding="utf-8")
            self.activate_review_context(repo_root)

            files = set(repo_tools.list_repo_files().splitlines())
            packet = repo_tools.read_repo_file(".github/agent-runtime/review/out/review-packet/index.md")

            with self.assertRaisesRegex(ValueError, "hidden review runtime/generated output"):
                repo_tools.read_repo_file(".agent-runtime/openai-agent-venv/lib/sdk.py")
            with self.assertRaisesRegex(ValueError, "hidden review runtime/generated output"):
                repo_tools.read_repo_file(".github/agent-runtime/review/out/review.json")
            with self.assertRaisesRegex(ValueError, "hidden review runtime/generated output"):
                repo_tools.run_shell_command("grep -R sdk .agent-runtime")
            shell_output = repo_tools.run_shell_command(
                "find . -name sdk.py -print; "
                "find . -name review.json -print; "
                "find .github/agent-runtime/review/out/review-packet -type f -print"
            )
            self.assertTrue(venv_file.is_file())
            self.assertTrue(review_output.is_file())

        self.assertIn("src.py", files)
        self.assertIn(".github/agent-runtime/review/out/review-packet/index.md", files)
        self.assertIn("packet", packet)
        self.assertIn(".github/agent-runtime/review/out/review-packet/index.md", shell_output)
        self.assertNotIn(".agent-runtime/openai-agent-venv/lib/sdk.py", shell_output)
        self.assertNotIn(".github/agent-runtime/review/out/review.json", shell_output)
        self.assertNotIn(".agent-runtime/openai-agent-venv/lib/sdk.py", files)
        self.assertNotIn(".github/agent-runtime/review/out/review.json", files)

    def test_review_shell_runs_from_tracked_source_snapshot(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_review_source_snapshot",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            subprocess.run(["git", "init", "-q"], cwd=repo_root, check=True)
            tracked_file = repo_root / "tracked.py"
            tracked_file.write_text("VALUE = 1\n", encoding="utf-8")
            review_out_gitignore = repo_root / ".github" / "agent-runtime" / "review" / "out" / ".gitignore"
            review_out_gitignore.parent.mkdir(parents=True)
            review_out_gitignore.write_text("*\n!.gitignore\n", encoding="utf-8")
            subprocess.run(
                ["git", "add", "tracked.py", ".github/agent-runtime/review/out/.gitignore"],
                cwd=repo_root,
                check=True,
            )
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Test",
                    "-c",
                    "user.email=test@example.invalid",
                    "commit",
                    "-qm",
                    "base",
                ],
                cwd=repo_root,
                check=True,
            )
            generated_file = repo_root / "random-generated" / "leak.txt"
            generated_file.parent.mkdir()
            generated_file.write_text("generated\n", encoding="utf-8")
            packet_index = repo_root / ".github" / "agent-runtime" / "review" / "out" / "review-packet" / "index.md"
            packet_index.parent.mkdir(parents=True)
            packet_index.write_text("packet\n", encoding="utf-8")
            self.activate_review_context(repo_root)

            git_output = repo_tools.run_shell_command("git status --short; git diff --name-status")
            output = repo_tools.run_shell_command(
                "find . -maxdepth 1 -print; "
                "find . -name tracked.py -print; "
                "find . -name leak.txt -print; "
                "find .github/agent-runtime/review/out/review-packet -type f -print"
            )
            tracked_stdin = repo_tools.run_shell_command("cat < tracked.py")
            env_output = repo_tools.run_shell_command("env")
            with self.assertRaisesRegex(ValueError, "not available in the shell workspace"):
                repo_tools.run_shell_command("cat < random-generated/leak.txt")
            with self.assertRaisesRegex(ValueError, "escapes shell workspace"):
                repo_tools.run_shell_command("cat < ..")
            with self.assertRaisesRegex(ValueError, "escapes shell workspace"):
                repo_tools.run_shell_command("find .. -name leak.txt -print")

        self.assertIn("$ git status --short", git_output)
        self.assertIn("$ git diff --name-status", git_output)
        self.assertNotIn("exit_code=128", git_output)
        self.assertNotIn("not a git repository", git_output)
        self.assertNotIn(".github/agent-runtime/review/out/.gitignore", git_output)
        self.assertNotIn("review-packet/index.md", git_output)
        self.assertNotIn("random-generated/leak.txt", git_output)
        self.assertNotIn("\n./.git\n", output)
        self.assertNotIn("GIT_DIR=", env_output)
        self.assertNotIn("GIT_WORK_TREE=", env_output)
        self.assertIn("./tracked.py", output)
        self.assertIn("VALUE = 1", tracked_stdin)
        self.assertIn(".github/agent-runtime/review/out/review-packet/index.md", output)
        self.assertNotIn("random-generated/leak.txt", output)

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

    def test_openai_agent_runner_applies_safe_quoted_unified_diff(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_patch_guard_quoted_safe",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            target = repo_root / "example path.txt"
            target.write_text("old\n", encoding="utf-8")
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(repo_root, 10)

            output = repo_tools.apply_unified_diff(
                textwrap.dedent(
                    """\
                    diff --git "a/example path.txt" "b/example path.txt"
                    --- "a/example path.txt"
                    +++ "b/example path.txt"
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

            quoted_escaping_patch = textwrap.dedent(
                """\
                diff --git "a/safe.txt" "b/../outside.txt"
                --- "a/safe.txt"
                +++ "b/../outside.txt"
                @@ -0,0 +1 @@
                +unsafe
                """
            )
            with self.assertRaisesRegex(ValueError, "escapes repository root"):
                repo_tools.apply_unified_diff(quoted_escaping_patch)

            escaped_quoted_patch = textwrap.dedent(
                """\
                diff --git "a/safe.txt" "b/escaped.txt"
                --- "a/safe.txt"
                +++ "b/escaped\\040path.txt"
                @@ -0,0 +1 @@
                +unsafe
                """
            )
            with self.assertRaisesRegex(ValueError, "Escaped patch paths are not supported"):
                repo_tools.apply_unified_diff(escaped_quoted_patch)

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
        with self.assertRaisesRegex(ValueError, "git push"):
            repo_tools.reject_unsafe_shell_command("git -c core.quotepath=false push")
        with self.assertRaisesRegex(ValueError, "git push"):
            repo_tools.reject_unsafe_shell_command("git -C . push")
        with self.assertRaisesRegex(ValueError, "git push"):
            repo_tools.reject_unsafe_shell_command("git --work-tree . push")
        with self.assertRaisesRegex(ValueError, "Command is intentionally blocked"):
            repo_tools.reject_unsafe_shell_command("git -c core.quotepath false push")
        with self.assertRaisesRegex(ValueError, "Unsupported shell syntax"):
            repo_tools.reject_unsafe_shell_command("git diff --check || true")
        with self.assertRaisesRegex(ValueError, "Unsupported empty command"):
            repo_tools.reject_unsafe_shell_command("git diff --check |")
        with self.assertRaisesRegex(ValueError, "stdout redirection is not supported"):
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
            "git -c core.quotepath=false status --short",
            "git -C . status --short",
            "git --git-dir=.git status --short",
            "git --work-tree . status --short",
            "git diff --output=/tmp/diff.patch",
        ):
            with self.assertRaisesRegex(ValueError, "Command is intentionally blocked"):
                repo_tools.reject_unsafe_shell_command(command)


if __name__ == "__main__":
    unittest.main()
