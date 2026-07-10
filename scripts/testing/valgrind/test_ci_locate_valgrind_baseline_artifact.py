#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import os
import subprocess
import tempfile
import textwrap
from typing import Dict, List, Optional
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
SCRIPT_PATH = SCRIPT_DIR / "ci-locate-valgrind-baseline-artifact.sh"
BASELINE_HELPER_PATH = SCRIPT_DIR / "ci_valgrind_baseline_artifact.py"


class TestCiLocateValgrindBaselineArtifact(unittest.TestCase):
    def test_selects_first_current_head_run_with_available_artifact(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            output_path = tmp / "github-output"
            result = self.run_locator(
                tmp,
                output_path,
                gh_script="""
                    case "$1 $2" in
                      "api repos/example/repo/git/ref/heads/develop")
                        printf '%s\\n' 'current-develop-sha'
                        ;;
                      "run list")
                        if ! printf '%s\\n' "$*" | grep -q -- 'databaseId,event'; then
                          printf 'run list did not request event: %s\\n' "$*" >&2
                          exit 2
                        fi
                        if ! printf '%s\\n' "$*" | grep -q -- 'workflow_dispatch'; then
                          printf 'run list did not filter baseline events: %s\\n' "$*" >&2
                          exit 2
                        fi
                        printf '%s\\n%s\\n' '303' '202'
                        ;;
                      "api repos/example/repo/actions/runs/303/artifacts")
                        printf '%s\\n' ''
                        ;;
                      "api repos/example/repo/actions/runs/202/artifacts")
                        printf '%s\\n' 'artifact-202'
                        ;;
                      *)
                        printf 'unexpected gh call: %s\\n' "$*" >&2
                        exit 2
                        ;;
                    esac
                """,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "Using valgrind-baseline artifact from run 202 at develop current-develop-sha.",
                result.stdout,
            )
            self.assertEqual(output_path.read_text(encoding="utf-8"), "run-id=202\n")

    def test_fails_when_current_head_has_no_available_artifact(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            output_path = tmp / "github-output"
            result = self.run_locator(
                tmp,
                output_path,
                gh_script="""
                    case "$1 $2" in
                      "api repos/example/repo/git/ref/heads/develop")
                        printf '%s\\n' 'current-develop-sha'
                        ;;
                      "run list")
                        printf '%s\\n' '303'
                        ;;
                      "api repos/example/repo/actions/runs/303/artifacts")
                        printf '%s\\n' ''
                        ;;
                      *)
                        printf 'unexpected gh call: %s\\n' "$*" >&2
                        exit 2
                        ;;
                    esac
                """,
            )

            self.assertEqual(result.returncode, 1)
            self.assertIn(
                "No available valgrind-baseline artifact found on develop at current-develop-sha.",
                result.stderr,
            )
            self.assertIn(
                "Publish valgrind.yml on the current develop tip to create a new baseline artifact.",
                result.stderr,
            )
            self.assertFalse(output_path.exists())

    def test_publish_script_skips_when_current_head_artifact_exists(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            result = self.run_script(
                tmp,
                BASELINE_HELPER_PATH,
                args=["publish"],
                gh_script="""
                    case "$1 $2" in
                      "api repos/example/repo/git/ref/heads/develop")
                        printf '%s\\n' 'current-develop-sha'
                        ;;
                      "run list")
                        printf '%s\\n' '[{"databaseId":202,"event":"workflow_dispatch","status":"completed"}]'
                        ;;
                      "api repos/example/repo/actions/runs/202/artifacts")
                        printf '%s\\n' '{"artifacts":[{"name":"valgrind-baseline","expired":false}]}'
                        ;;
                      *)
                        printf 'unexpected gh call: %s\\n' "$*" >&2
                        exit 2
                        ;;
                    esac
                """,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("Baseline artifact already exists in run 202.", result.stdout)

    def test_publish_script_ignores_pull_request_target_artifact_candidates(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            result = self.run_script(
                tmp,
                BASELINE_HELPER_PATH,
                args=["publish"],
                gh_script="""
                    case "$1 $2" in
                      "api repos/example/repo/git/ref/heads/develop")
                        printf '%s\\n' 'current-develop-sha'
                        ;;
                      "run list")
                        printf '%s\\n' '[{"databaseId":303,"event":"pull_request_target","status":"completed"},{"databaseId":202,"event":"workflow_dispatch","status":"completed"}]'
                        ;;
                      "api repos/example/repo/actions/runs/202/artifacts")
                        printf '%s\\n' '{"artifacts":[{"name":"valgrind-baseline","expired":false}]}'
                        ;;
                      "api repos/example/repo/actions/runs/303/artifacts")
                        printf 'unexpected pull_request_target artifact lookup\\n' >&2
                        exit 2
                        ;;
                      *)
                        printf 'unexpected gh call: %s\\n' "$*" >&2
                        exit 2
                        ;;
                    esac
                """,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("Baseline artifact already exists in run 202.", result.stdout)

    def test_publish_script_skips_when_current_head_run_is_active(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            result = self.run_script(
                tmp,
                BASELINE_HELPER_PATH,
                args=["publish"],
                gh_script="""
                    case "$1 $2" in
                      "api repos/example/repo/git/ref/heads/develop")
                        printf '%s\\n' 'current-develop-sha'
                        ;;
                      "run list")
                        if printf '%s\\n' "$*" | grep -q -- '--status success'; then
                          printf '%s\\n' '[]'
                        else
                          printf '%s\\n' '[{"databaseId":404,"event":"workflow_dispatch","status":"in_progress"}]'
                        fi
                        ;;
                      *)
                        printf 'unexpected gh call: %s\\n' "$*" >&2
                        exit 2
                        ;;
                    esac
                """,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("Baseline run 404 is already active.", result.stdout)

    def test_publish_script_ignores_current_pull_request_target_run(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            result = self.run_script(
                tmp,
                BASELINE_HELPER_PATH,
                args=["publish"],
                extra_env={"GITHUB_RUN_ID": "404"},
                gh_script="""
                    case "$1 $2" in
                      "api repos/example/repo/git/ref/heads/develop")
                        printf '%s\\n' 'current-develop-sha'
                        ;;
                      "run list")
                        if printf '%s\\n' "$*" | grep -q -- '--status success'; then
                          printf '%s\\n' '[]'
                        else
                          printf '%s\\n' '[{"databaseId":404,"event":"pull_request_target","status":"in_progress"}]'
                        fi
                        ;;
                      "workflow run")
                        printf '%s\\n' "$*" > "${TMPDIR}/dispatch"
                        ;;
                      *)
                        printf 'unexpected gh call: %s\\n' "$*" >&2
                        exit 2
                        ;;
                    esac
                """,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((tmp / "dispatch").read_text(encoding="utf-8"),
                             "workflow run valgrind.yml --ref develop\n")

    def test_publish_script_dispatches_when_no_artifact_or_active_run_exists(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            result = self.run_script(
                tmp,
                BASELINE_HELPER_PATH,
                args=["publish"],
                gh_script="""
                    case "$1 $2" in
                      "api repos/example/repo/git/ref/heads/develop")
                        printf '%s\\n' 'current-develop-sha'
                        ;;
                      "run list")
                        printf '%s\\n' '[]'
                        ;;
                      "workflow run")
                        printf '%s\\n' "$*" > "${TMPDIR}/dispatch"
                        ;;
                      *)
                        printf 'unexpected gh call: %s\\n' "$*" >&2
                        exit 2
                        ;;
                    esac
                """,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((tmp / "dispatch").read_text(encoding="utf-8"),
                             "workflow run valgrind.yml --ref develop\n")

    def test_wait_script_polls_quietly_until_artifact_exists(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            output_path = tmp / "github-output"
            attempts_file = tmp / "attempts"
            attempts_file.write_text("0", encoding="utf-8")
            result = self.run_script(
                tmp,
                BASELINE_HELPER_PATH,
                args=["wait"],
                output_path=output_path,
                extra_env={
                    "VALGRIND_BASELINE_POLL_SECONDS": "0",
                    "VALGRIND_BASELINE_TIMEOUT_SECONDS": "10",
                    "TMPDIR": str(tmp),
                },
                gh_script="""
                    case "$1 $2" in
                      "api repos/example/repo/git/ref/heads/develop")
                        printf '%s\\n' 'current-develop-sha'
                        ;;
                      "run list")
                        if printf '%s\\n' "$*" | grep -q -- '--status success'; then
                          attempts="$(cat "${TMPDIR}/attempts")"
                          attempts="$((attempts + 1))"
                          printf '%s\\n' "${attempts}" > "${TMPDIR}/attempts"
                          if [ "${attempts}" -lt 3 ]; then
                            printf '%s\\n' '[]'
                          else
                            printf '%s\\n' '[{"databaseId":202,"event":"workflow_dispatch","status":"completed"}]'
                          fi
                        else
                          printf '%s\\n' '[{"databaseId":404,"event":"workflow_dispatch","status":"in_progress"}]'
                        fi
                        ;;
                      "api repos/example/repo/actions/runs/202/artifacts")
                        printf '%s\\n' '{"artifacts":[{"name":"valgrind-baseline","expired":false}]}'
                        ;;
                      *)
                        printf 'unexpected gh call: %s\\n' "$*" >&2
                        exit 2
                        ;;
                    esac
                """,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("Waiting for valgrind-baseline artifact on develop current-develop-sha.", result.stdout)
            self.assertIn(
                "Using valgrind-baseline artifact from run 202 at develop current-develop-sha.",
                result.stdout,
            )
            self.assertNotIn("No available valgrind-baseline artifact", result.stdout)
            self.assertEqual(output_path.read_text(encoding="utf-8"), "run-id=202\n")

    def run_locator(self, tmp: Path, output_path: Path, gh_script: str) -> subprocess.CompletedProcess:
        return self.run_script(tmp, SCRIPT_PATH, gh_script, output_path=output_path)

    def run_script(
        self,
        tmp: Path,
        script_path: Path,
        gh_script: str,
        args: Optional[List[str]] = None,
        output_path: Optional[Path] = None,
        extra_env: Optional[Dict[str, str]] = None,
    ) -> subprocess.CompletedProcess:
        bin_dir = tmp / "bin"
        bin_dir.mkdir()
        gh_path = bin_dir / "gh"
        gh_path.write_text(
            "#!/usr/bin/env bash\n"
            "set -euo pipefail\n"
            f"{textwrap.dedent(gh_script)}",
            encoding="utf-8",
        )
        gh_path.chmod(0o755)

        env = os.environ.copy()
        env.update(
            {
                "GITHUB_REPOSITORY": "example/repo",
                "GITHUB_OUTPUT": str(output_path or tmp / "github-output"),
                "PATH": f"{bin_dir}:{env['PATH']}",
                "TMPDIR": str(tmp),
            }
        )
        if extra_env:
            env.update(extra_env)

        return subprocess.run(
            [str(script_path), *(args or [])],
            check=False,
            env=env,
            text=True,
            capture_output=True,
        )


if __name__ == "__main__":
    unittest.main()
