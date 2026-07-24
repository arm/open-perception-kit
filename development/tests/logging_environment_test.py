#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################


"""Fresh-process tests for logging environment initialization."""

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class LoggingEnvironmentTest(unittest.TestCase):
    probe = sys.argv[1]

    def run_probe(self, action, *, level=None, targets=None, log_file=None, cwd=None):
        environment = os.environ.copy()
        environment.pop("OPK_LOG_LEVEL", None)
        environment.pop("OPK_LOG_TARGETS", None)
        environment.pop("OPK_LOG_FILE", None)
        if level is not None:
            environment["OPK_LOG_LEVEL"] = level
        if targets is not None:
            environment["OPK_LOG_TARGETS"] = targets
        if log_file is not None:
            environment["OPK_LOG_FILE"] = log_file
        return subprocess.run(
            [self.probe, action],
            env=environment,
            cwd=cwd,
            check=True,
            capture_output=True,
            text=True,
        )

    def test_missing_values_report_defaults_in_order(self):
        result = self.run_probe("targets")
        self.assertEqual(result.stdout, "stdout\n")
        self.assertEqual(
            result.stderr,
            "OPK_LOG_LEVEL is not set; defaulting to 4 (Info).\n"
            "OPK_LOG_TARGETS is not set; defaulting to stdout.\n",
        )

    def test_level_is_parsed_clamped_and_malformed_values_default_silently(self):
        self.assertEqual(self.run_probe("level", level="2", targets="none").stdout, "2\n")
        self.assertEqual(self.run_probe("level", level="9", targets="none").stdout, "4\n")
        self.assertEqual(self.run_probe("level", level="99", targets="none").stdout, "4\n")
        malformed = self.run_probe("level", level="bad", targets="none")
        self.assertEqual(malformed.stdout, "4\n")
        self.assertEqual(malformed.stderr, "")

    def test_unknown_and_duplicate_targets_are_ignored(self):
        result = self.run_probe(
            "targets", level="4", targets="unknown,stderr,stderr,stdout"
        )
        self.assertEqual(result.stdout, "stdout\nstderr\n")
        self.assertEqual(result.stderr, "")

    def test_no_recognized_target_defaults_to_stdout(self):
        result = self.run_probe("targets", level="4", targets="unknown")
        self.assertEqual(result.stdout, "stdout\n")
        self.assertEqual(result.stderr, "")

    def test_none_disables_all_targets(self):
        result = self.run_probe("emit", level="4", targets="none")
        self.assertEqual(result.stdout, "")
        self.assertEqual(result.stderr, "")

    def test_every_severity_is_sent_to_each_selected_target(self):
        result = self.run_probe("emit", level="4", targets="stdout,stderr")
        self.assertEqual(result.stdout, "info\nE: error\n")
        self.assertEqual(result.stderr, "info\nE: error\n")

    def test_file_is_disabled_without_exact_target_token(self):
        with tempfile.TemporaryDirectory() as directory:
            log_file = Path(directory, "configured.log")

            self.assertEqual(self.run_probe("targets", level="4", cwd=directory).stdout,
                             "stdout\n")
            self.run_probe("emit", level="4", log_file=str(log_file), cwd=directory)
            self.run_probe(
                "emit", level="4", targets="File", log_file=str(log_file), cwd=directory
            )

            self.assertFalse(Path(directory, "opk.log").exists())
            self.assertFalse(log_file.exists())

    def test_file_target_lazily_uses_default_and_custom_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            directory_path = Path(directory)

            self.run_probe("enable", level="4", targets="none", cwd=directory)
            self.assertFalse(Path(directory, "opk.log").exists())

            for name, log_file in (
                ("unset", None),
                ("empty", ""),
                ("relative", "relative.log"),
                ("absolute", str(directory_path / "absolute.log")),
            ):
                with self.subTest(name=name):
                    working_directory = directory_path / name
                    working_directory.mkdir()
                    result = self.run_probe(
                        "emit",
                        level="4",
                        targets="file",
                        log_file=log_file,
                        cwd=working_directory,
                    )
                    expected_path = (
                        Path(log_file)
                        if log_file and Path(log_file).is_absolute()
                        else working_directory / (log_file or "opk.log")
                    )
                    self.assertEqual(result.stdout, "")
                    self.assertEqual(result.stderr, "")
                    self.assertEqual(expected_path.read_bytes(), b"info\nerror\n")

    def test_file_output_is_raw_and_can_share_console_output(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_probe(
                "emit", level="4", targets="stdout,file", cwd=directory
            )

            self.assertEqual(result.stdout, "info\nE: error\n")
            self.assertEqual(Path(directory, "opk.log").read_bytes(), b"info\nerror\n")

    def test_runtime_disable_closes_and_reenable_appends_lazily(self):
        with tempfile.TemporaryDirectory() as directory:
            log_file = Path(directory, "runtime.log")

            self.run_probe(
                "toggle", level="4", targets="none", log_file=str(log_file), cwd=directory
            )
            self.assertEqual(log_file.read_bytes(), b"beforeafter")

            self.run_probe(
                "emit", level="4", targets="file", log_file=str(log_file), cwd=directory
            )
            self.assertEqual(log_file.read_bytes(), b"beforeafterinfo\nerror\n")

    def test_open_failure_disables_only_file_target(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_probe(
                "emit-targets",
                level="4",
                targets="stdout,file",
                log_file="missing/directory/log.txt",
                cwd=directory,
            )

            self.assertEqual(result.stdout, "info\nstdout\n")
            self.assertEqual(result.stderr, "")

    @unittest.skipUnless(Path("/dev/full").exists(), "/dev/full is unavailable")
    def test_flush_failure_disables_file_target(self):
        result = self.run_probe(
            "emit-targets", level="4", targets="file", log_file="/dev/full"
        )

        self.assertEqual(result.stdout, "")
        self.assertEqual(result.stderr, "")


if __name__ == "__main__":
    del sys.argv[1:]
    unittest.main()
