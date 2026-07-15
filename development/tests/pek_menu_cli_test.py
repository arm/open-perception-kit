#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import json
import os
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path


class TestPekMenuCliDiagnostics(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.pek_menu = Path(sys.argv[1]).resolve()

    def run_cli(
        self,
        *args: str,
        env_overrides: dict[str, str] | None = None,
    ) -> subprocess.CompletedProcess[str]:
        env = os.environ.copy()
        env["OPK_LOG_LEVEL"] = "0"
        if env_overrides:
            env.update(env_overrides)

        return subprocess.run(
            [str(self.pek_menu), *args],
            check=False,
            capture_output=True,
            env=env,
            text=True,
        )

    @staticmethod
    def write_pipeline(path: Path, pipeline: object) -> None:
        path.write_text(
            json.dumps({"description": "CLI diagnostic test", "pipeline": pipeline}),
            encoding="utf-8",
        )

    def assert_stderr_diagnostic(
        self,
        result: subprocess.CompletedProcess[str],
        returncode: int,
        message: str,
    ) -> None:
        self.assertEqual(result.returncode, returncode)
        self.assertEqual(result.stdout, "")
        self.assertIn(message, result.stderr)

    def assert_stdout_diagnostic(
        self,
        result: subprocess.CompletedProcess[str],
        returncode: int,
        message: str,
    ) -> None:
        self.assertEqual(result.returncode, returncode)
        self.assertIn(message, result.stdout)
        self.assertEqual(result.stderr, "")

    def test_usage_is_written_to_stdout(self) -> None:
        result = self.run_cli("-h")

        self.assert_stdout_diagnostic(result, 2, "Usage:")

    def test_invalid_arguments_write_usage_to_stdout(self) -> None:
        result = self.run_cli("-x")

        self.assert_stdout_diagnostic(result, 2, "Usage:")

    def test_missing_pipeline_diagnostic_is_not_suppressed(self) -> None:
        result = self.run_cli("pek-menu-pipeline-that-does-not-exist")

        self.assert_stdout_diagnostic(result, 3, "Pipeline not found:")

    def test_json_parse_diagnostic_is_not_suppressed(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            pipeline = Path(tmpdir) / "malformed.json"
            pipeline.write_text("{", encoding="utf-8")

            result = self.run_cli(str(pipeline))

        self.assertEqual(result.returncode, 3)
        self.assertIn("Failed to load pipeline from:", result.stdout)
        self.assertNotIn("JSON parse error", result.stdout)
        self.assertIn("JSON parse error", result.stderr)
        self.assertNotIn("Failed to load pipeline from:", result.stderr)

    def test_json_schema_diagnostics_are_not_suppressed(self) -> None:
        invalid_documents = {
            "missing-description": {"pipeline": "fakesrc ! fakesink"},
            "missing-pipeline": {"description": "missing pipeline"},
            "non-string-array": {
                "description": "invalid array",
                "pipeline": ["fakesrc", 42],
            },
            "empty-array": {"description": "empty array", "pipeline": []},
            "invalid-pipeline-type": {
                "description": "invalid type",
                "pipeline": 42,
            },
        }

        with tempfile.TemporaryDirectory() as tmpdir:
            for name, document in invalid_documents.items():
                with self.subTest(name=name):
                    pipeline = Path(tmpdir) / f"{name}.json"
                    pipeline.write_text(json.dumps(document), encoding="utf-8")

                    result = self.run_cli(str(pipeline))

                    self.assertEqual(result.returncode, 3)
                    self.assertIn("Failed to load pipeline from:", result.stdout)
                    self.assertNotIn("Invalid JSON", result.stdout)
                    self.assertIn("Invalid JSON", result.stderr)
                    self.assertNotIn("Failed to load pipeline from:", result.stderr)

    def test_tokenization_diagnostic_is_not_suppressed(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            pipeline = Path(tmpdir) / "unterminated-quote.json"
            self.write_pipeline(pipeline, 'fakesrc name="unterminated')

            result = self.run_cli("-p", str(pipeline))

        self.assert_stderr_diagnostic(
            result,
            3,
            "error: Unterminated quote in pipeline string",
        )

    def test_dry_run_keeps_command_on_stdout(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            pipeline = Path(tmpdir) / "dry-run.json"
            self.write_pipeline(pipeline, "fakesrc ! fakesink")

            result = self.run_cli("-p", str(pipeline))

        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "gst-launch-1.0 fakesrc ! fakesink \n")
        self.assertEqual(result.stderr, "")

    def test_exec_failure_diagnostic_is_not_suppressed(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            pipeline = Path(tmpdir) / "exec-failure.json"
            self.write_pipeline(pipeline, "fakesrc ! fakesink")

            result = self.run_cli(
                str(pipeline),
                env_overrides={"PATH": "/pek-menu-test-path-does-not-exist"},
            )

        self.assertEqual(result.returncode, 127)
        self.assertEqual(result.stdout, "gst-launch-1.0 fakesrc ! fakesink \n")
        self.assertIn("execvp:", result.stderr)

    def test_looping_pipeline_stops_after_forwarded_sigint(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            temp_dir = Path(tmpdir)
            bin_dir = temp_dir / "bin"
            bin_dir.mkdir()
            marker = temp_dir / "starts"

            fake_gst_launch = bin_dir / "gst-launch-1.0"
            fake_gst_launch.write_text(
                "#!/bin/sh\n"
                "trap 'exit 0' INT TERM\n"
                'printf "started\\n" >> "$PEK_MENU_TEST_MARKER"\n'
                "while true; do sleep 0.05; done\n",
                encoding="utf-8",
            )
            fake_gst_launch.chmod(0o755)

            pipeline = temp_dir / "looping.json"
            pipeline.write_text(
                json.dumps(
                    {
                        "description": "Looping pipeline signal test",
                        "loop": True,
                        "pipeline": "fakesrc ! fakesink",
                    }
                ),
                encoding="utf-8",
            )

            env = os.environ.copy()
            env["OPK_LOG_LEVEL"] = "0"
            env["PATH"] = f"{bin_dir}:{env['PATH']}"
            env["PEK_MENU_TEST_MARKER"] = str(marker)

            process = subprocess.Popen(
                [str(self.pek_menu), str(pipeline)],
                env=env,
                start_new_session=True,
                stderr=subprocess.PIPE,
                stdout=subprocess.PIPE,
                text=True,
            )

            try:
                for _ in range(100):
                    if marker.exists() or process.poll() is not None:
                        break
                    time.sleep(0.01)

                self.assertTrue(marker.exists(), "pipeline child did not start")

                process.send_signal(signal.SIGINT)
                stdout, stderr = process.communicate(timeout=2)
            finally:
                if process.poll() is None:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.communicate()

            self.assertEqual(process.returncode, 128 + signal.SIGINT)
            self.assertEqual(marker.read_text(encoding="utf-8"), "started\n")
            self.assertNotIn("Pipeline reached EOS; restarting.", stdout)
            self.assertEqual(stderr, "")


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
