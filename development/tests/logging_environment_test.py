#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################


"""Fresh-process tests for logging environment initialization."""

import os
import subprocess
import sys
import unittest


class LoggingEnvironmentTest(unittest.TestCase):
    probe = sys.argv[1]

    def run_probe(self, action, *, level=None, targets=None):
        environment = os.environ.copy()
        environment.pop("OPK_LOG_LEVEL", None)
        environment.pop("OPK_LOG_TARGETS", None)
        if level is not None:
            environment["OPK_LOG_LEVEL"] = level
        if targets is not None:
            environment["OPK_LOG_TARGETS"] = targets
        return subprocess.run(
            [self.probe, action],
            env=environment,
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


if __name__ == "__main__":
    del sys.argv[1:]
    unittest.main()
