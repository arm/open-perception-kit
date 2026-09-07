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

PROCESS_TIMEOUT_SECONDS = 5.0
STARTUP_TIMEOUT_SECONDS = 15.0
DESCENDANT_TIMEOUT_SECONDS = 2.0
POLL_INTERVAL_SECONDS = 0.01


class TestPekMenuCliDiagnostics(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.pek_menu = Path(sys.argv[1]).resolve()
        cls.project_root = Path(__file__).resolve().parents[2]

    def run_cli(
        self,
        *args: str,
        env_overrides: dict[str, str] | None = None,
    ) -> subprocess.CompletedProcess[str]:
        env = os.environ.copy()
        env["OPK_LOG_LEVEL"] = "0"
        env["PEK_PROJECT_ROOT"] = str(self.project_root)
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

    def test_project_root_controls_pipeline_discovery_and_expansion(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            project_root = Path(tmpdir) / "project root"
            pipelines_dir = project_root / "config" / "pipelines"
            pipelines_dir.mkdir(parents=True)
            pipeline = pipelines_dir / "portable.json"
            self.write_pipeline(
                pipeline,
                "filesrc location=${PEK_PROJECT_ROOT:-/work}/data/example.mp4 ! fakesink",
            )
            selection_file = pipelines_dir / ".last_selected_pipeline_id"
            selection_file.write_text(
                "/work/config/pipelines/portable.json\n",
                encoding="utf-8",
            )
            environment = {"PEK_PROJECT_ROOT": str(project_root)}

            selected_by_id = self.run_cli("-p", "portable", env_overrides=environment)
            selected_as_last = self.run_cli("-p", "-l", env_overrides=environment)
            self.assertEqual(selection_file.read_text(encoding="utf-8"), "portable.json\n")

        expected = (
            f"gst-launch-1.0 filesrc location={project_root}/data/example.mp4 ! fakesink \n"
        )
        for result in (selected_by_id, selected_as_last):
            self.assertEqual(result.returncode, 0)
            self.assertEqual(result.stdout, expected)
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
            marker = temp_dir / "process-events"

            descendant = bin_dir / "pipeline-descendant"
            descendant.write_text(
                "#!/usr/bin/env python3\n"
                "import os\n"
                "import signal\n"
                "import time\n"
                "\n"
                "marker = os.environ['PEK_MENU_TEST_MARKER']\n"
                "\n"
                "def stop(_signal_number, _frame):\n"
                "    with open(marker, 'a', encoding='utf-8') as output:\n"
                "        output.write('descendant-stopped\\n')\n"
                "    raise SystemExit(0)\n"
                "\n"
                "signal.signal(signal.SIGINT, stop)\n"
                "signal.signal(signal.SIGTERM, stop)\n"
                "with open(marker, 'a', encoding='utf-8') as output:\n"
                "    output.write('descendant-started\\n')\n"
                "while True:\n"
                "    time.sleep(0.05)\n",
                encoding="utf-8",
            )
            descendant.chmod(0o755)

            fake_gst_launch = bin_dir / "gst-launch-1.0"
            fake_gst_launch.write_text(
                "#!/usr/bin/env python3\n"
                "import os\n"
                "import signal\n"
                "import subprocess\n"
                "import time\n"
                "\n"
                "parent_pid = os.getppid()\n"
                "descendant = subprocess.Popen([os.environ['PEK_MENU_TEST_DESCENDANT']])\n"
                "\n"
                "def stop(_signal_number, _frame):\n"
                "    try:\n"
                f"        descendant.wait(timeout={DESCENDANT_TIMEOUT_SECONDS})\n"
                "    except subprocess.TimeoutExpired:\n"
                "        descendant.kill()\n"
                f"        descendant.wait(timeout={DESCENDANT_TIMEOUT_SECONDS})\n"
                "    raise SystemExit(0)\n"
                "\n"
                "signal.signal(signal.SIGINT, stop)\n"
                "signal.signal(signal.SIGTERM, stop)\n"
                "while os.getppid() == parent_pid:\n"
                "    time.sleep(0.05)\n"
                "descendant.kill()\n"
                f"descendant.wait(timeout={DESCENDANT_TIMEOUT_SECONDS})\n",
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
            env["PEK_PROJECT_ROOT"] = str(self.project_root)
            env["PATH"] = f"{bin_dir}:{env['PATH']}"
            env["PEK_MENU_TEST_MARKER"] = str(marker)
            env["PEK_MENU_TEST_DESCENDANT"] = str(descendant)

            process = subprocess.Popen(
                [str(self.pek_menu), str(pipeline)],
                env=env,
                start_new_session=True,
                stderr=subprocess.PIPE,
                stdout=subprocess.PIPE,
                text=True,
            )

            try:
                readiness_deadline = time.monotonic() + STARTUP_TIMEOUT_SECONDS
                while time.monotonic() < readiness_deadline:
                    if (
                        marker.exists()
                        and "descendant-started"
                        in marker.read_text(encoding="utf-8")
                    ) or process.poll() is not None:
                        break
                    time.sleep(POLL_INTERVAL_SECONDS)

                self.assertTrue(marker.exists(), "pipeline descendant did not start")
                self.assertIn(
                    "descendant-started",
                    marker.read_text(encoding="utf-8"),
                )

                process.send_signal(signal.SIGINT)
                stdout, stderr = process.communicate(timeout=PROCESS_TIMEOUT_SECONDS)
            finally:
                if process.poll() is None:
                    try:
                        process.kill()
                    except ProcessLookupError:
                        pass
                process.communicate(timeout=PROCESS_TIMEOUT_SECONDS)

            self.assertEqual(process.returncode, 128 + signal.SIGINT)
            self.assertEqual(
                marker.read_text(encoding="utf-8"),
                "descendant-started\ndescendant-stopped\n",
            )
            self.assertNotIn("Pipeline reached EOS; restarting.", stdout)
            self.assertEqual(stderr, "")

    def test_looping_pipeline_stops_when_eos_is_immediate(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            temp_dir = Path(tmpdir)
            bin_dir = temp_dir / "bin"
            bin_dir.mkdir()
            marker = temp_dir / "starts"

            fake_gst_launch = bin_dir / "gst-launch-1.0"
            fake_gst_launch.write_text(
                "#!/bin/sh\n"
                'printf "started\\n" >> "$PEK_MENU_TEST_MARKER"\n'
                'test "$(wc -l < "$PEK_MENU_TEST_MARKER")" -eq 1 || exit 7\n'
                "exit 0\n",
                encoding="utf-8",
            )
            fake_gst_launch.chmod(0o755)

            pipeline = temp_dir / "looping.json"
            pipeline.write_text(
                json.dumps(
                    {
                        "description": "Immediate EOS test",
                        "loop": True,
                        "pipeline": "fakesrc ! fakesink",
                    }
                ),
                encoding="utf-8",
            )

            result = self.run_cli(
                str(pipeline),
                env_overrides={
                    "PATH": f"{bin_dir}:{os.environ['PATH']}",
                    "PEK_MENU_TEST_MARKER": str(marker),
                },
            )

            self.assertEqual(result.returncode, 1)
            self.assertEqual(marker.read_text(encoding="utf-8"), "started\n")
            self.assertNotIn("Pipeline reached EOS; restarting.", result.stdout)
            self.assertIn(
                "Pipeline reached EOS too quickly; refusing to restart.",
                result.stderr,
            )


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
