#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import json
import os
import pty
import select
import signal
import subprocess
import sys
import tempfile
import termios
import time
import unittest
import wave
from pathlib import Path

PROCESS_TIMEOUT_SECONDS = 5.0
STARTUP_TIMEOUT_SECONDS = 15.0
POLL_INTERVAL_SECONDS = 0.01


class TestOpkMenuCliDiagnostics(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.opk_menu = Path(sys.argv[1]).resolve()
        cls.project_root = Path(__file__).resolve().parents[2]

    def run_cli(
        self,
        *args: str,
        env_overrides: dict[str, str] | None = None,
    ) -> subprocess.CompletedProcess[str]:
        env = os.environ.copy()
        env["GST_DEBUG"] = "0"
        env["OPK_LOG_LEVEL"] = "0"
        env["OPK_PROJECT_ROOT"] = str(self.project_root)
        if env_overrides:
            env.update(env_overrides)

        return subprocess.run(
            [str(self.opk_menu), *args],
            check=False,
            capture_output=True,
            env=env,
            text=True,
        )

    @staticmethod
    def write_pipeline(path: Path, pipeline: object) -> None:
        path.write_text(
            json.dumps(
                {"version": "1.0.0", "description": "CLI diagnostic test", "pipeline": pipeline}
            ),
            encoding="utf-8",
        )

    @staticmethod
    def write_menu_project(project_root: Path) -> None:
        pipelines_dir = project_root / "config" / "pipelines"
        pipelines_dir.mkdir(parents=True)
        pipelines = {
            "01-first.json": {
                "version": "1.0.0",
                "description": "First pipeline",
                "sourceInfo": "file source",
                "pipeline": "fakesrc name=first ! fakesink",
            },
            "02-second.json": {
                "version": "1.0.0",
                "description": "Second pipeline",
                "sourceInfo": "USB camera required",
                "pipeline": "fakesrc name=second ! fakesink",
            },
        }
        for name, document in pipelines.items():
            (pipelines_dir / name).write_text(json.dumps(document), encoding="utf-8")

    def run_menu_in_pty(
        self,
        project_root: Path,
        input_bytes: bytes,
        env_overrides: dict[str, str] | None = None,
    ) -> tuple[int, bytes, list[object], list[object]]:
        env = os.environ.copy()
        env["GST_DEBUG"] = "0"
        env["OPK_LOG_LEVEL"] = "0"
        env["OPK_PROJECT_ROOT"] = str(project_root)
        env["TERM"] = "xterm-256color"
        if env_overrides:
            env.update(env_overrides)

        master_fd, slave_fd = pty.openpty()
        original_attributes = termios.tcgetattr(slave_fd)
        process = subprocess.Popen(
            [str(self.opk_menu), "-p"],
            env=env,
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
        )
        output = bytearray()

        try:
            startup_deadline = time.monotonic() + STARTUP_TIMEOUT_SECONDS
            while b"q/Esc: quit" not in output and time.monotonic() < startup_deadline:
                readable, _, _ = select.select([master_fd], [], [], POLL_INTERVAL_SECONDS)
                if readable:
                    output.extend(os.read(master_fd, 65536))
                if process.poll() is not None:
                    break

            self.assertIn(b"q/Esc: quit", output, "interactive menu did not start")
            os.write(master_fd, input_bytes)

            exit_deadline = time.monotonic() + PROCESS_TIMEOUT_SECONDS
            while process.poll() is None and time.monotonic() < exit_deadline:
                readable, _, _ = select.select([master_fd], [], [], POLL_INTERVAL_SECONDS)
                if readable:
                    output.extend(os.read(master_fd, 65536))

            if process.poll() is None:
                process.kill()
                self.fail("interactive menu did not exit")
            process.wait(timeout=PROCESS_TIMEOUT_SECONDS)

            while True:
                readable, _, _ = select.select([master_fd], [], [], 0)
                if not readable:
                    break
                output.extend(os.read(master_fd, 65536))

            restored_attributes = termios.tcgetattr(slave_fd)
            return (
                process.returncode,
                bytes(output),
                original_attributes,
                restored_attributes,
            )
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=PROCESS_TIMEOUT_SECONDS)
            os.close(master_fd)
            os.close(slave_fd)

    def run_command_in_pty(
        self,
        args: tuple[str, ...],
        input_bytes: bytes = b"",
        project_root: Path | None = None,
        env_overrides: dict[str, str] | None = None,
    ) -> tuple[int, bytes]:
        env = os.environ.copy()
        env["GST_DEBUG"] = "0"
        env["OPK_LOG_LEVEL"] = "0"
        env["OPK_PROJECT_ROOT"] = str(project_root or self.project_root)
        env["TERM"] = "xterm-256color"
        if env_overrides:
            env.update(env_overrides)

        master_fd, slave_fd = pty.openpty()
        process = subprocess.Popen(
            [str(self.opk_menu), *args],
            env=env,
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
        )
        output = bytearray()

        try:
            if input_bytes:
                os.write(master_fd, input_bytes)

            exit_deadline = time.monotonic() + PROCESS_TIMEOUT_SECONDS
            while process.poll() is None and time.monotonic() < exit_deadline:
                readable, _, _ = select.select([master_fd], [], [], POLL_INTERVAL_SECONDS)
                if readable:
                    output.extend(os.read(master_fd, 65536))

            if process.poll() is None:
                process.kill()
                self.fail("PTY command did not exit")
            process.wait(timeout=PROCESS_TIMEOUT_SECONDS)

            while True:
                readable, _, _ = select.select([master_fd], [], [], 0)
                if not readable:
                    break
                output.extend(os.read(master_fd, 65536))

            return process.returncode, bytes(output)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=PROCESS_TIMEOUT_SECONDS)
            os.close(master_fd)
            os.close(slave_fd)

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
        self.assertIn("--log-level LEVEL", result.stdout)
        self.assertIn("default: error", result.stdout)
        self.assertIn("--log-targets TARGETS", result.stdout)
        self.assertIn("default: stderr", result.stdout)

    def test_invalid_arguments_write_usage_to_stdout(self) -> None:
        result = self.run_cli("-x")

        self.assert_stdout_diagnostic(result, 2, "Usage:")

    def test_invalid_runtime_logging_options_are_rejected(self) -> None:
        invalid_options = {
            "level": ("--log-level", "verbose", "Invalid log level 'verbose'."),
            "targets": (
                "--log-targets",
                "stdout,network",
                "Invalid log targets 'stdout,network'.",
            ),
        }

        for name, (option, value, diagnostic) in invalid_options.items():
            with self.subTest(name=name):
                result = self.run_cli(option, value)

                self.assertEqual(result.returncode, 2)
                self.assertIn("Usage:", result.stdout)
                self.assertIn(diagnostic, result.stderr)

    def test_missing_pipeline_diagnostic_is_not_suppressed(self) -> None:
        result = self.run_cli("opk-menu-pipeline-that-does-not-exist")

        self.assert_stdout_diagnostic(result, 3, "Pipeline not found:")

    def test_json_parse_diagnostic_is_not_suppressed(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            pipeline = Path(tmpdir) / "malformed.json"
            pipeline.write_text("{", encoding="utf-8")

            result = self.run_cli(str(pipeline))

        self.assertEqual(result.returncode, 3)
        self.assertIn("Failed to load pipeline from:", result.stdout)
        self.assertNotIn("parse json.syntax", result.stdout)
        self.assertIn("parse json.syntax", result.stderr)
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
            "invalid-loop-type": {
                "description": "invalid loop",
                "loop": "yes",
                "pipeline": "fakesrc ! fakesink",
            },
            "empty-pipeline-string": {
                "description": "empty pipeline",
                "pipeline": "",
            },
        }

        with tempfile.TemporaryDirectory() as tmpdir:
            for name, document in invalid_documents.items():
                with self.subTest(name=name):
                    document.setdefault("version", "1.0.0")
                    pipeline = Path(tmpdir) / f"{name}.json"
                    pipeline.write_text(json.dumps(document), encoding="utf-8")

                    result = self.run_cli(str(pipeline))

                    self.assertEqual(result.returncode, 3)
                    self.assertIn("Failed to load pipeline from:", result.stdout)
                    self.assertNotIn("schema schema.validation", result.stdout)
                    self.assertIn("schema schema.validation", result.stderr)
                    self.assertNotIn("Failed to load pipeline from:", result.stderr)

    def test_invalid_versions_fail_before_pipeline_expansion(self) -> None:
        invalid_versions = (None, True, 1, "1", 1.0, 0, -1, "0.9.0", "2.0.0", "1.0.0-rc1")

        with tempfile.TemporaryDirectory() as tmpdir:
            pipeline = Path(tmpdir) / "invalid-version.json"
            for version in invalid_versions:
                with self.subTest(version=version):
                    document = {
                        "description": "Version test",
                        "pipeline": "${OPK_REQUIRED_PIPELINE_VALUE?should not expand}",
                    }
                    if version is not None:
                        document["version"] = version
                    pipeline.write_text(json.dumps(document), encoding="utf-8")

                    result = self.run_cli("-p", str(pipeline))

                    self.assertEqual(result.returncode, 3)
                    self.assertIn(str(pipeline), result.stderr)
                    self.assertIn("/version", result.stderr)
                    self.assertIn("supported version: 1.0.0", result.stderr)
                    self.assertNotIn("should not expand", result.stderr)

    def test_runtime_parse_diagnostic_is_not_suppressed(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            pipeline = Path(tmpdir) / "invalid-launch-syntax.json"
            self.write_pipeline(pipeline, "fakesrc !")

            result = self.run_cli(str(pipeline))

        self.assertEqual(result.returncode, 1)
        self.assertEqual(
            result.stdout,
            "gst-launch-1.0 fakesrc ! \n",
        )
        self.assertIn("Pipeline setup failed:", result.stderr)

    def test_dry_run_keeps_command_on_stdout(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            pipeline = Path(tmpdir) / "dry-run.json"
            self.write_pipeline(pipeline, "fakesrc ! fakesink")

            result = self.run_cli("-p", str(pipeline))

        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "gst-launch-1.0 fakesrc ! fakesink \n")
        self.assertEqual(result.stderr, "")

    def test_missing_discovered_files_stop_before_pipeline_setup(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            project_root = Path(tmpdir)
            pipeline_dir = project_root / "config" / "pipelines"
            model_dir = project_root / "config" / "models" / "missing"
            media_file = project_root / "data" / "missing.mp4"
            pipeline_dir.mkdir(parents=True)
            model_dir.mkdir(parents=True)
            (model_dir / "model.json").write_text(
                json.dumps(
                    {
                        "version": "1.0.0",
                        "name": "missing",
                        "modelFile": "missing.onnx",
                        "dynamicOutput": True,
                        "inputTensors": [
                            {
                                "shape": [1],
                                "dataKind": "RawTensorData",
                                "valueType": "Float32",
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            (model_dir / "opchain.json").write_text(
                json.dumps(
                    {
                        "version": "1.0.0",
                        "name": "missing",
                        "description": "Missing files.",
                        "ops": [
                            {
                                "id": "opk-future-ops/Inference",
                                "attributes": {"modelDescriptor": "model.json"},
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            self.write_pipeline(
                pipeline_dir / "missing-files.json",
                (
                    f'filesrc location="{media_file}" ! '
                    f'opkinfer opchain-path="{model_dir / "opchain.json"}" ! '
                    "fakesink"
                ),
            )

            result = self.run_cli(
                "missing-files", env_overrides={"OPK_PROJECT_ROOT": str(project_root)}
            )

        self.assertEqual(result.returncode, 1)
        self.assertIn(f"Model: MISSING {model_dir / 'missing.onnx'}", result.stdout)
        self.assertIn(f"Media: MISSING {media_file}", result.stdout)
        self.assertIn("Missing model files: 1", result.stdout)
        self.assertIn("Missing media files: 1", result.stdout)
        self.assertIn(
            "You have model files missing, and you have media files missing.", result.stdout
        )
        self.assertIn(
            "Use scripts/download-models.py --models-dir config/models to download models.",
            result.stdout,
        )
        self.assertIn(
            "Use scripts/private/download-demo-videos.sh to download media.",
            result.stdout,
        )
        self.assertNotIn("Pipeline setup failed:", result.stdout)
        self.assertEqual(result.stderr, "")

    def test_missing_executorch_plugin_stops_before_pipeline_setup(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            project_root = Path(tmpdir)
            pipeline_dir = project_root / "config" / "pipelines"
            model_dir = project_root / "config" / "models" / "executorch"
            plugin_dir = project_root / "plugins"
            ops_dir = project_root / "ops"
            pipeline_dir.mkdir(parents=True)
            model_dir.mkdir(parents=True)
            plugin_dir.mkdir(parents=True)
            ops_dir.mkdir(parents=True)
            (model_dir / "model.pte").write_bytes(b"model")
            (model_dir / "model.json").write_text(
                json.dumps(
                    {
                        "version": "1.0.0",
                        "name": "executorch",
                        "modelFile": "model.pte",
                        "dynamicOutput": True,
                        "inputTensors": [
                            {
                                "shape": [1],
                                "dataKind": "RawTensorData",
                                "valueType": "Float32",
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            (model_dir / "opchain.json").write_text(
                json.dumps(
                    {
                        "version": "1.0.0",
                        "name": "executorch",
                        "description": "ExecuTorch plugin check.",
                        "ops": [
                            {
                                "id": "opk-executorch-ops/Inference",
                                "attributes": {"modelDescriptor": "model.json"},
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            self.write_pipeline(
                pipeline_dir / "executorch.json",
                f'opkinfer opchain-path="{model_dir / "opchain.json"}" ! fakesink',
            )

            result = self.run_cli(
                "executorch",
                env_overrides={
                    "OPK_PROJECT_ROOT": str(project_root),
                    "OPK_PLUGIN_PATH": str(plugin_dir),
                    "OPK_OPS_PATH": str(ops_dir),
                },
            )

        plugin = ops_dir / "opk-executorch-ops.so"
        self.assertEqual(result.returncode, 1)
        self.assertIn(f"Model: OK {model_dir / 'model.pte'}", result.stdout)
        self.assertIn(f"ExecuTorch plugin: MISSING {plugin}", result.stdout)
        self.assertIn(
            "ExecuTorch model detected, but the ExecuTorch OPK ops plugin was not found.",
            result.stdout,
        )
        self.assertIn("Build OPK with ExecuTorch support enabled", result.stdout)
        self.assertNotIn("Pipeline setup failed:", result.stdout)
        self.assertEqual(result.stderr, "")

    def test_filesink_output_path_is_not_required_by_preflight(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            temp_dir = Path(tmpdir)
            pipeline = temp_dir / "filesink-output.json"
            output_file = temp_dir / "new-output.mp4"
            self.write_pipeline(
                pipeline,
                f'videotestsrc num-buffers=1 ! filesink location="{output_file}"',
            )

            result = self.run_cli(str(pipeline))

        self.assertEqual(result.returncode, 0)
        self.assertIn("gst-launch-1.0 videotestsrc num-buffers=1", result.stdout)
        self.assertNotIn("Media: MISSING", result.stdout)
        self.assertNotIn("Download the missing files", result.stdout)
        self.assertEqual(result.stderr, "")

    def test_direct_tty_missing_media_returns_failure_without_escape_prompt(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            temp_dir = Path(tmpdir)
            pipeline = temp_dir / "missing-media.json"
            media_file = temp_dir / "missing.mp4"
            self.write_pipeline(pipeline, f'filesrc location="{media_file}" ! fakesink')

            returncode, output = self.run_command_in_pty((str(pipeline),))

        self.assertEqual(returncode, 1)
        self.assertIn(b"Media: MISSING", output)
        self.assertIn(b"Download the missing files", output)
        self.assertNotIn(b"Press ESC", output)

    def test_runtime_logging_options_are_applied_to_every_selected_target(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            temp_dir = Path(tmpdir)
            pipeline = temp_dir / "qos.json"
            log_file = temp_dir / "opk.log"
            self.write_pipeline(
                pipeline,
                (
                    "videotestsrc num-buffers=3 is-live=true ! "
                    "identity sleep-time=100000 ! "
                    "fakesink name=qos_sink sync=true qos=true max-lateness=0"
                ),
            )

            result = self.run_cli(
                "--log-level",
                "debug",
                "--log-targets",
                "stdout,stderr,file",
                str(pipeline),
                env_overrides={"OPK_LOG_FILE": str(log_file)},
            )

            file_output = log_file.read_text(encoding="utf-8")

        self.assertEqual(result.returncode, 0)
        for output in (result.stdout, result.stderr, file_output):
            self.assertIn("GStreamer QoS: source=qos_sink", output)

    def test_redirected_menu_keeps_line_oriented_numeric_selection(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            project_root = Path(tmpdir)
            self.write_menu_project(project_root)
            env = os.environ.copy()
            env["GST_DEBUG"] = "0"
            env["OPK_LOG_LEVEL"] = "0"
            env["OPK_PROJECT_ROOT"] = str(project_root)

            result = subprocess.run(
                [str(self.opk_menu), "-p"],
                check=False,
                capture_output=True,
                env=env,
                input="2\n",
                text=True,
            )

        self.assertEqual(result.returncode, 0)
        self.assertIn("2 -> 02-second.json [Second pipeline]", result.stdout)
        self.assertIn("gst-launch-1.0 fakesrc name=second ! fakesink", result.stdout)
        self.assertNotIn("\x1b[", result.stdout)
        self.assertEqual(result.stderr, "")

    def test_interactive_menu_supports_cursor_selection(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            project_root = Path(tmpdir)
            self.write_menu_project(project_root)

            returncode, output, original_attributes, restored_attributes = (
                self.run_menu_in_pty(project_root, b"\x1b[B\r")
            )

        self.assertEqual(returncode, 0)
        self.assertIn(b"OPEN PERCEPTION KIT - Demo Pipeline Launcher", output)
        self.assertIn(b"gst-launch-1.0 fakesrc name=second ! fakesink", output)
        self.assertIn(b"\x1b[?25l", output)
        self.assertIn(b"\x1b[?25h", output)
        self.assertEqual(restored_attributes, original_attributes)

    def test_interactive_menu_keeps_numeric_selection(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            project_root = Path(tmpdir)
            self.write_menu_project(project_root)

            returncode, output, _, _ = self.run_menu_in_pty(project_root, b"2\r")

        self.assertEqual(returncode, 0)
        self.assertIn(b"Selection: 2  (USB camera required)", output)
        self.assertIn(b"gst-launch-1.0 fakesrc name=second ! fakesink", output)

    def test_interactive_zero_runs_last_selected_pipeline(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            project_root = Path(tmpdir)
            self.write_menu_project(project_root)
            selection_file = (
                project_root / "config" / "pipelines" / ".last_selected_pipeline_id"
            )
            selection_file.write_text("02-second.json\n", encoding="utf-8")

            returncode, output, _, _ = self.run_menu_in_pty(project_root, b"0\r")

        self.assertEqual(returncode, 0)
        self.assertIn(b"Last used: 02-second", output)
        self.assertIn(b"gst-launch-1.0 fakesrc name=second ! fakesink", output)

    def test_interactive_menu_ignores_stale_last_selected_pipeline(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            project_root = Path(tmpdir)
            self.write_menu_project(project_root)
            selection_file = (
                project_root / "config" / "pipelines" / ".last_selected_pipeline_id"
            )
            selection_file.write_text("01-removed.json\n", encoding="utf-8")

            returncode, output, _, _ = self.run_menu_in_pty(project_root, b"0\r1\r")
            saved_selection = selection_file.read_text(encoding="utf-8")

        self.assertEqual(returncode, 0)
        self.assertIn(b"Last used: (no previous selection)", output)
        self.assertIn(b"No previous selection stored", output)
        self.assertIn(b"gst-launch-1.0 fakesrc name=first ! fakesink", output)
        self.assertEqual(saved_selection, "01-first.json\n")

    def test_interactive_menu_sanitizes_terminal_control_characters(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            project_root = Path(tmpdir)
            self.write_menu_project(project_root)
            pipeline = project_root / "config" / "pipelines" / "02-second.json"
            pipeline.write_text(
                json.dumps(
                    {
                        "version": "1.0.0",
                        "description": "Untrusted \x1b[31m description",
                        "pipeline": "fakesrc name=second ! fakesink",
                    }
                ),
                encoding="utf-8",
            )

            returncode, output, _, _ = self.run_menu_in_pty(project_root, b"q")

        self.assertEqual(returncode, 0)
        self.assertNotIn(b"\x1b[31m", output)

    def test_no_color_disables_sgr_styles_but_keeps_interactive_input(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            project_root = Path(tmpdir)
            self.write_menu_project(project_root)

            returncode, output, _, _ = self.run_menu_in_pty(
                project_root, b"q", env_overrides={"NO_COLOR": "1"}
            )

        self.assertEqual(returncode, 0)
        self.assertIn(b"\x1b[?25l", output)
        self.assertNotIn(b"\x1b[1;36m", output)
        self.assertNotIn(b"\x1b[1;33m", output)

    def test_interactive_ctrl_c_restores_terminal_and_returns_signal_status(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            project_root = Path(tmpdir)
            self.write_menu_project(project_root)

            returncode, output, original_attributes, restored_attributes = (
                self.run_menu_in_pty(project_root, b"\x03")
            )

        self.assertEqual(returncode, 128 + signal.SIGINT)
        self.assertIn(b"\x1b[?25h", output)
        self.assertEqual(restored_attributes, original_attributes)

    def test_project_root_controls_pipeline_discovery_and_expansion(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            project_root = Path(tmpdir) / "project root"
            pipelines_dir = project_root / "config" / "pipelines"
            pipelines_dir.mkdir(parents=True)
            pipeline = pipelines_dir / "portable.json"
            self.write_pipeline(
                pipeline,
                "filesrc location=${OPK_PROJECT_ROOT:-/work}/data/example.mp4 ! fakesink",
            )
            selection_file = pipelines_dir / ".last_selected_pipeline_id"
            selection_file.write_text(
                "/work/config/pipelines/portable.json\n",
                encoding="utf-8",
            )
            environment = {"OPK_PROJECT_ROOT": str(project_root)}

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

    def test_last_ignores_stale_saved_pipeline(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            project_root = Path(tmpdir)
            self.write_menu_project(project_root)
            selection_file = (
                project_root / "config" / "pipelines" / ".last_selected_pipeline_id"
            )
            selection_file.write_text("01-removed.json\n", encoding="utf-8")

            result = self.run_cli(
                "-p", "-l", env_overrides={"OPK_PROJECT_ROOT": str(project_root)}
            )
            selection_file_exists = selection_file.exists()

        self.assertEqual(result.returncode, 3)
        self.assertIn("No valid previous selection stored", result.stdout)
        self.assertNotIn("Last selected pipeline", result.stdout)
        self.assertEqual(result.stderr, "")
        self.assertFalse(selection_file_exists)

    def test_missing_element_diagnostic_is_not_suppressed(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            pipeline = Path(tmpdir) / "missing-element.json"
            self.write_pipeline(
                pipeline,
                "opk_menu_element_that_does_not_exist ! fakesink",
            )

            result = self.run_cli(str(pipeline))

        self.assertEqual(result.returncode, 1)
        self.assertEqual(
            result.stdout,
            "gst-launch-1.0 opk_menu_element_that_does_not_exist ! fakesink \n",
        )
        self.assertIn("Pipeline setup failed:", result.stderr)

    def test_runtime_pipeline_stops_after_sigint(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            temp_dir = Path(tmpdir)
            pipeline = temp_dir / "live.json"
            pipeline.write_text(
                json.dumps(
                    {
                        "version": "1.0.0",
                        "description": "Runtime pipeline signal test",
                        "pipeline": "videotestsrc is-live=true ! fakesink sync=false",
                    }
                ),
                encoding="utf-8",
            )

            env = os.environ.copy()
            env["GST_DEBUG"] = "0"
            env["OPK_LOG_LEVEL"] = "0"
            env["OPK_PROJECT_ROOT"] = str(self.project_root)

            process = subprocess.Popen(
                [str(self.opk_menu), str(pipeline)],
                env=env,
                stderr=subprocess.PIPE,
                stdout=subprocess.PIPE,
                text=True,
            )

            try:
                time.sleep(0.25)
                self.assertIsNone(process.poll(), "Runtime pipeline exited before SIGINT")

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
            self.assertIn("gst-launch-1.0 videotestsrc is-live=true", stdout)
            self.assertEqual(stderr, "")

    def test_looping_pipeline_seeks_after_eos_until_sigterm(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            temp_dir = Path(tmpdir)
            pipeline = temp_dir / "looping.json"
            media = temp_dir / "loop.wav"
            with wave.open(str(media), "wb") as wav_file:
                wav_file.setnchannels(1)
                wav_file.setsampwidth(1)
                wav_file.setframerate(8_000)
                wav_file.writeframes(bytes([0x80]) * 800)
            pipeline.write_text(
                json.dumps(
                    {
                        "version": "1.0.0",
                        "description": "Runtime loop test",
                        "loop": True,
                        "pipeline": (
                            f'filesrc location="{media}" ! '
                            "wavparse ! fakesink sync=true"
                        ),
                    }
                ),
                encoding="utf-8",
            )

            env = os.environ.copy()
            env["GST_DEBUG"] = "0"
            env["OPK_LOG_LEVEL"] = "0"
            env["OPK_PROJECT_ROOT"] = str(self.project_root)
            process = subprocess.Popen(
                [str(self.opk_menu), str(pipeline)],
                env=env,
                stderr=subprocess.PIPE,
                stdout=subprocess.PIPE,
                text=True,
            )

            try:
                time.sleep(0.5)
                self.assertIsNone(process.poll(), "looping Runtime pipeline stopped at EOS")
                process.send_signal(signal.SIGTERM)
                stdout, stderr = process.communicate(timeout=PROCESS_TIMEOUT_SECONDS)
            finally:
                if process.poll() is None:
                    process.kill()
                process.communicate(timeout=PROCESS_TIMEOUT_SECONDS)

            self.assertEqual(process.returncode, 128 + signal.SIGTERM)
            self.assertIn("gst-launch-1.0 filesrc", stdout)
            self.assertEqual(stderr, "")


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
