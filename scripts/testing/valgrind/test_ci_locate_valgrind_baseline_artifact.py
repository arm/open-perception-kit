#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import os
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
BASELINE_HELPER_PATH = SCRIPT_DIR / "ci_valgrind_baseline_artifact.py"


class TestCiLocateValgrindBaselineArtifact(unittest.TestCase):
    def test_locate_selects_first_current_head_run_with_available_artifact(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            output_path = tmp / "github-output"
            result = self.run_script(
                tmp,
                args=["locate"],
                output_path=output_path,
                runs='[{"databaseId":303,"event":"workflow_dispatch","status":"completed"},{"databaseId":202,"event":"push","status":"completed"}]',
                artifact_runs={202},
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
            result = self.run_script(
                tmp,
                args=["locate"],
                output_path=output_path,
                runs='[{"databaseId":303,"event":"workflow_dispatch","status":"completed"}]',
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
                args=["publish"],
                runs='[{"databaseId":202,"event":"workflow_dispatch","status":"completed"}]',
                artifact_runs={202},
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("Baseline artifact already exists in run 202.", result.stdout)

    def test_publish_script_ignores_pull_request_target_artifact_candidates(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            result = self.run_script(
                tmp,
                args=["publish"],
                runs='[{"databaseId":303,"event":"pull_request_target","status":"completed"},{"databaseId":202,"event":"workflow_dispatch","status":"completed"}]',
                artifact_runs={202},
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("Baseline artifact already exists in run 202.", result.stdout)

    def test_publish_script_skips_when_current_head_run_is_active(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            result = self.run_script(
                tmp,
                args=["publish"],
                runs='[{"databaseId":404,"event":"workflow_dispatch","status":"in_progress"}]',
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("Baseline run 404 is already active.", result.stdout)

    def test_publish_script_dispatches_when_no_artifact_or_active_run_exists(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            result = self.run_script(
                tmp,
                args=["publish"],
                runs="[]",
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
                args=["wait"],
                output_path=output_path,
                extra_env={
                    "VALGRIND_BASELINE_POLL_SECONDS": "0",
                    "VALGRIND_BASELINE_TIMEOUT_SECONDS": "10",
                    "TMPDIR": str(tmp),
                },
                runs='[{"databaseId":404,"event":"workflow_dispatch","status":"in_progress"}]',
                success_runs_after_attempts='[{"databaseId":202,"event":"workflow_dispatch","status":"completed"}]',
                artifact_runs={202},
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("Waiting for valgrind-baseline artifact on develop current-develop-sha.", result.stdout)
            self.assertIn(
                "Using valgrind-baseline artifact from run 202 at develop current-develop-sha.",
                result.stdout,
            )
            self.assertNotIn("No available valgrind-baseline artifact", result.stdout)
            self.assertEqual(output_path.read_text(encoding="utf-8"), "run-id=202\n")

    def run_script(
        self,
        tmp: Path,
        args,
        output_path=None,
        extra_env=None,
        runs="[]",
        success_runs_after_attempts=None,
        artifact_runs=frozenset(),
    ) -> subprocess.CompletedProcess:
        bin_dir = tmp / "bin"
        bin_dir.mkdir()
        gh_path = bin_dir / "gh"
        gh_path.write_text(
            textwrap.dedent(
                """\
                #!/usr/bin/env python3
                import json
                import os
                import sys
                from pathlib import Path

                args = sys.argv[1:]
                tmp = Path(os.environ["TMPDIR"])

                if args[:2] == ["api", "repos/example/repo/git/ref/heads/develop"]:
                    print("current-develop-sha")
                elif args[:2] == ["run", "list"]:
                    if "--status" in args and os.environ.get("SUCCESS_RUNS_AFTER_ATTEMPTS"):
                        attempts = int((tmp / "attempts").read_text(encoding="utf-8")) + 1
                        (tmp / "attempts").write_text(str(attempts), encoding="utf-8")
                        print("[]" if attempts < 3 else os.environ["SUCCESS_RUNS_AFTER_ATTEMPTS"])
                    else:
                        print(os.environ["RUNS"])
                elif len(args) >= 2 and args[0] == "api" and args[1].endswith("/artifacts"):
                    run_id = args[1].split("/")[-2]
                    artifact_runs = set(os.environ["ARTIFACT_RUNS"].split())
                    artifacts = [{"name": "valgrind-baseline", "expired": False}] if run_id in artifact_runs else []
                    print(json.dumps({"artifacts": artifacts}))
                elif args[:2] == ["workflow", "run"]:
                    (tmp / "dispatch").write_text(" ".join(args) + "\\n", encoding="utf-8")
                else:
                    print(f"unexpected gh call: {' '.join(args)}", file=sys.stderr)
                    raise SystemExit(2)
                """
            ),
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
                "RUNS": runs,
                "ARTIFACT_RUNS": " ".join(str(run_id) for run_id in sorted(artifact_runs)),
            }
        )
        if success_runs_after_attempts is not None:
            env["SUCCESS_RUNS_AFTER_ATTEMPTS"] = success_runs_after_attempts
        if extra_env:
            env.update(extra_env)

        return subprocess.run(
            [str(BASELINE_HELPER_PATH), *args],
            check=False,
            env=env,
            text=True,
            capture_output=True,
        )


if __name__ == "__main__":
    unittest.main()
