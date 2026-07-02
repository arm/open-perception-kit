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


SCRIPT_PATH = Path(__file__).with_name("ci-locate-valgrind-baseline-artifact.sh")


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
                      "api repos/example/repo/git/ref/heads/main")
                        printf '%s\\n' 'current-main-sha'
                        ;;
                      "run list")
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
                "Using valgrind-baseline artifact from run 202 at main current-main-sha.",
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
                      "api repos/example/repo/git/ref/heads/main")
                        printf '%s\\n' 'current-main-sha'
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
                "No available valgrind-baseline artifact found on main at current-main-sha.",
                result.stderr,
            )
            self.assertFalse(output_path.exists())

    def run_locator(self, tmp: Path, output_path: Path, gh_script: str) -> subprocess.CompletedProcess:
        bin_dir = tmp / "bin"
        bin_dir.mkdir()
        gh_path = bin_dir / "gh"
        gh_path.write_text(
            textwrap.dedent(
                f"""\
                #!/usr/bin/env bash
                set -euo pipefail
                {textwrap.dedent(gh_script)}
                """
            ),
            encoding="utf-8",
        )
        gh_path.chmod(0o755)

        env = os.environ.copy()
        env.update(
            {
                "GITHUB_REPOSITORY": "example/repo",
                "GITHUB_OUTPUT": str(output_path),
                "PATH": f"{bin_dir}:{env['PATH']}",
            }
        )

        return subprocess.run(
            [str(SCRIPT_PATH)],
            check=False,
            env=env,
            text=True,
            capture_output=True,
        )


if __name__ == "__main__":
    unittest.main()
