################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

import ast
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[3]
PACKAGE_ROOT = REPO_ROOT / "tools/expkits-ci"
PYPROJECT_FILE = PACKAGE_ROOT / "pyproject.toml"
EXPKITS_CI_SOURCE = PACKAGE_ROOT / "expkits_ci/expkits_ci.py"
PRE_COMMIT_CONFIG = REPO_ROOT / ".pre-commit-config.yaml"
CI_COMPOSE_FILE = REPO_ROOT / ".github/compose.ci.yaml"
PEK_CI_WORKFLOW = REPO_ROOT / ".github/workflows/pek-ci.yml"
VALGRIND_WORKFLOW = REPO_ROOT / ".github/workflows/valgrind.yml"
BASELINE_FILE = REPO_ROOT / ".secrets.baseline"


def make_private_key_fixture():
    """Build a detectable synthetic private-key payload without embedding the literal line in the repo."""
    payload_line = "".join([
        "MIIEvQIBADANBgkq",
        "hkiG9w0BAQEFAASC",
        "BKcwggSjAgEAAoIB",
        "AQDArandomlookin",
        "gsecret",
    ])
    return "\n".join([
        "-----BEGIN " + "PRIVATE KEY-----",
        payload_line,
        "-----END PRIVATE KEY-----",
        "",
    ])


class DetectSecretsQualityFlowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.test_python = os.environ.get("EXPKITS_CI_TEST_PYTHON", sys.executable)
        runtime_probe = subprocess.run(
            [
                cls.test_python,
                "-c",
                "import argcomplete, git, detect_secrets.pre_commit_hook",
            ],
            capture_output=True,
            text=True,
        )
        if runtime_probe.returncode != 0:
            raise unittest.SkipTest(
                "expkits-ci integration runtime is unavailable; set EXPKITS_CI_TEST_PYTHON to a venv "
                "with argcomplete, GitPython and detect-secrets installed."
            )

    def setUp(self):
        self.work_dir = tempfile.TemporaryDirectory()
        self.repo_dir = Path(self.work_dir.name) / "repo"
        self.repo_dir.mkdir()
        self.run_cmd(["git", "init", "-b", "main"])
        self.run_cmd(["git", "config", "user.name", "Detect Secrets Tests"])
        self.run_cmd(["git", "config", "user.email", "detect-secrets-tests@example.com"])
        shutil.copy2(BASELINE_FILE, self.repo_dir / ".secrets.baseline")

    def tearDown(self):
        self.work_dir.cleanup()

    def run_cmd(self, cmd, check=True, env=None):
        return subprocess.run(
            cmd,
            cwd=self.repo_dir,
            env=env,
            check=check,
            capture_output=True,
            text=True,
        )

    def run_expkits_ci(self, *args, check=False):
        env = os.environ.copy()
        env["PYTHONPATH"] = str(PACKAGE_ROOT) + os.pathsep + env.get("PYTHONPATH", "")
        return self.run_cmd([self.test_python, "-m", "expkits_ci", *args], check=check, env=env)

    def run_repo_python(self, code):
        env = os.environ.copy()
        env["PYTHONPATH"] = str(PACKAGE_ROOT) + os.pathsep + env.get("PYTHONPATH", "")
        return subprocess.run(
            [self.test_python, "-c", code],
            cwd=REPO_ROOT,
            env=env,
            check=False,
            capture_output=True,
            text=True,
        )

    def commit_all(self, message="Seed repo for detect-secrets test"):
        self.run_cmd(["git", "add", "."])
        self.run_cmd(["git", "commit", "-m", message])

    def write_file(self, relative_path, content):
        target = self.repo_dir / relative_path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8")
        return target

    def test_cli_writes_report_file_for_safe_explicit_scope(self):
        self.write_file("safe.txt", "safe\n")
        self.commit_all()
        report_file = self.repo_dir / "artifacts" / "expkits-ci-report.txt"

        result = self.run_expkits_ci(
            "--check-secrets",
            "--list-of-files",
            "safe.txt",
            "--report-file",
            str(report_file),
        )

        self.assertEqual(result.returncode, 0, msg=result.stdout + result.stderr)
        self.assertIn("[INFO] expkits-ci execution plan:", result.stdout)
        self.assertIn("[INFO]   enabled checks: --check-secrets", result.stdout)
        self.assertIn("[INFO]   OK   secrets", result.stdout)
        self.assertIn("[INFO]   OK", result.stdout)
        report_contents = report_file.read_text(encoding="utf-8")
        self.assertIn("expkits-ci report", report_contents)
        self.assertIn("check results:", report_contents)
        self.assertIn("  OK   secrets", report_contents)
        self.assertTrue(report_contents.rstrip().endswith("overall: OK"))

    def test_cli_respects_file_only_log_routing(self):
        self.write_file("safe.txt", "safe\n")
        self.commit_all()
        log_file = self.repo_dir / "expkits-ci.log"
        report_file = self.repo_dir / "expkits-ci-report.txt"

        result = self.run_expkits_ci(
            "--check-secrets",
            "--list-of-files",
            "safe.txt",
            "--log-output",
            "file",
            "--log-file",
            str(log_file),
            "--report-file",
            str(report_file),
        )

        self.assertEqual(result.returncode, 0, msg=result.stdout + result.stderr)
        self.assertEqual(result.stdout, "")
        log_contents = log_file.read_text(encoding="utf-8")
        self.assertIn("[INFO] expkits-ci execution plan:", log_contents)
        self.assertIn("[INFO]   enabled checks: --check-secrets", log_contents)
        self.assertIn("[INFO] expkits-ci result summary:", log_contents)
        self.assertIn("[INFO]   OK   secrets", log_contents)
        self.assertTrue(log_contents.rstrip().endswith("[INFO]   OK"))
        self.assertIn("overall: OK", report_file.read_text(encoding="utf-8"))

    def test_cli_non_verbose_secret_failure_surfaces_detect_secrets_details(self):
        self.write_file("bad.pem", make_private_key_fixture())
        self.commit_all()

        result = self.run_expkits_ci(
            "--check-secrets",
            "--list-of-files",
            "bad.pem",
        )

        combined_output = result.stdout + result.stderr
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Secret Type: Private Key", combined_output)
        self.assertIn("Location:    bad.pem:1", combined_output)
        self.assertIn("[INFO]   NOK  secrets", combined_output)
        self.assertIn("One or more checks failed.", combined_output)

    def test_cli_explicit_empty_scope_does_not_fallback_to_full_repo_scan(self):
        self.write_file("bad.pem", make_private_key_fixture())
        self.write_file("safe.txt", "safe\n")
        self.commit_all()

        result = self.run_expkits_ci(
            "--check-secrets",
            "--list-of-files",
            "missing.txt",
        )

        self.assertEqual(result.returncode, 0, msg=result.stdout + result.stderr)
        self.assertIn("[INFO]   resolved files: 0", result.stdout)
        self.assertIn("[INFO]   OK   secrets", result.stdout)

    def test_cli_without_explicit_scope_scans_git_tracked_files(self):
        self.write_file("safe.txt", "safe\n")
        self.commit_all()

        result = self.run_expkits_ci("--check-secrets")

        self.assertEqual(result.returncode, 0, msg=result.stdout + result.stderr)
        self.assertIn("[INFO]   file scope: all tracked git files", result.stdout)
        self.assertIn("[INFO]   resolved files: 2", result.stdout)
        self.assertIn("[INFO]   OK   secrets", result.stdout)

    def test_repo_check_secrets_without_scope_returns_bool(self):
        result = self.run_repo_python(
            "from expkits_ci.quality_checks import QualityChecks; "
            "scan_result = QualityChecks().check_secrets(files=None, baseline='.secrets.baseline'); "
            "print(type(scan_result).__name__); "
            "print(scan_result)"
        )

        self.assertEqual(result.returncode, 0, msg=result.stdout + result.stderr)
        output_lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
        self.assertEqual(output_lines[-2:], ["bool", "True"])


class StaticQualityConfigTests(unittest.TestCase):
    def test_repo_configs_enable_secret_scan_and_quality_report_artifacts(self):
        pre_commit = PRE_COMMIT_CONFIG.read_text(encoding="utf-8")
        compose = CI_COMPOSE_FILE.read_text(encoding="utf-8")
        workflow = PEK_CI_WORKFLOW.read_text(encoding="utf-8")
        valgrind_workflow = VALGRIND_WORKFLOW.read_text(encoding="utf-8")

        self.assertIn("- id: check-secrets", pre_commit)
        self.assertIn("--check-secrets --list-of-files", pre_commit)
        self.assertIn("- id: actionlint", pre_commit)
        self.assertIn("--actionlint --list-of-files", pre_commit)
        self.assertIn(r"files: ^\.github/workflows/.*\.ya?ml$", pre_commit)
        self.assertNotIn("- id: agent-runtime-static-analysis", pre_commit)
        self.assertNotIn("--agent-runtime-static-analysis", pre_commit)
        self.assertIn("expkits-ci --all-checks --pr-target-branch ${PULL_REQUEST_TARGET_BRANCH}", compose)
        self.assertIn("--agent-runtime-static-analysis", compose)
        self.assertIn('if [ -n "$${PULL_REQUEST_TARGET_BRANCH:-}" ]; then', compose)
        self.assertIn('--pr-target-branch "$${PULL_REQUEST_TARGET_BRANCH}"', compose)
        self.assertIn("--report-file /work/.github/artifacts/expkits-ci-pr-report.txt", compose)
        self.assertIn("--report-file /work/.github/artifacts/expkits-ci-full-report.txt", compose)
        self.assertIn("Upload quality report artifact (PR)", workflow)
        self.assertIn("Upload quality report artifact (nightly)", workflow)
        self.assertIn("expkits-ci-quality-report-pr", workflow)
        self.assertIn("expkits-ci-quality-report-full", workflow)
        self.assertNotIn("pr-quality-gate:", workflow)
        self.assertNotIn("Finalize PR quality gate result", workflow)
        self.assertNotIn("git_basic_auth=", workflow)
        self.assertIn("source scripts/private/ci_git_auth_env.sh", workflow)
        self.assertIn("Resolve manual PR context", workflow)
        self.assertIn("python3 scripts/private/github_pr_context.py", workflow)
        self.assertNotIn("gh pr view", workflow)
        self.assertIn(
            "export PULL_REQUEST_TARGET_BRANCH=\"${{ steps.manual_pr.outputs.base_ref || github.base_ref }}\"",
            workflow,
        )
        self.assertIn("-e PULL_REQUEST_TARGET_BRANCH", workflow)
        self.assertIn(
            "if: ${{ !cancelled() && (github.event_name == 'pull_request' || github.event_name == 'schedule' ||",
            workflow,
        )
        self.assertNotIn("Run Valgrind checks", workflow)
        self.assertIn("name: Valgrind Baseline Artifact", valgrind_workflow)
        self.assertIn("branches: [main, develop]", valgrind_workflow)
        self.assertIn("branches: [main, develop, \"feature/**\", \"sandbox/**\"]", valgrind_workflow)
        self.assertIn("Locate latest Valgrind baseline artifact", valgrind_workflow)
        self.assertIn("Compare Valgrind results to baseline", valgrind_workflow)
        self.assertIn(
            "if: ${{ always() && !cancelled() && (steps.valgrind_checks.outcome == 'failure'",
            valgrind_workflow,
        )
        self.assertEqual(pre_commit.count('--list-of-files "$@"'), 9)

        pyproject = PYPROJECT_FILE.read_text(encoding="utf-8")
        self.assertIn('"mypy==1.16.1"', pyproject)
        self.assertIn('"pyflakes==3.3.2"', pyproject)
        self.assertIn('"vulture==2.14"', pyproject)

    def test_execution_report_annotations_match_declared_python_floor(self):
        pyproject = PYPROJECT_FILE.read_text(encoding="utf-8")
        self.assertIn('requires-python = ">=3.8"', pyproject)

        module = ast.parse(EXPKITS_CI_SOURCE.read_text(encoding="utf-8"), filename=str(EXPKITS_CI_SOURCE))
        typing_import = next(
            node
            for node in module.body
            if isinstance(node, ast.ImportFrom) and node.module == "typing"
        )
        imported_names = {alias.name for alias in typing_import.names}
        self.assertTrue({"List", "Optional"}.issubset(imported_names))

        execution_report = next(
            node
            for node in module.body
            if isinstance(node, ast.ClassDef) and node.name == "ExecutionReport"
        )
        annotations = {
            statement.target.id: statement.annotation
            for statement in execution_report.body
            if isinstance(statement, ast.AnnAssign) and isinstance(statement.target, ast.Name)
        }

        self.assertIsInstance(annotations["enabled_checks"], ast.Subscript)
        self.assertEqual(annotations["enabled_checks"].value.id, "List")
        self.assertIsInstance(annotations["pr_target_branch"], ast.Subscript)
        self.assertEqual(annotations["pr_target_branch"].value.id, "Optional")
        self.assertIsInstance(annotations["check_results"], ast.Subscript)
        self.assertEqual(annotations["check_results"].value.id, "List")


if __name__ == "__main__":
    unittest.main()
