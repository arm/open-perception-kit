################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[3]
PACKAGE_ROOT = REPO_ROOT / "tools/expkits-ci"
sys.path.insert(0, str(PACKAGE_ROOT))


CLANG_TIDY_LOG = """\
../common/pek/Shape.h:33:9: warning: do not declare C-style arrays [modernize-avoid-c-arrays]
   33 |     int temporary[] = {1, 2};
      |     ^
../common/pek/Shape.h:98:5: warning: do not declare C-style arrays [modernize-avoid-c-arrays]
   98 |     int dims[8] = {0};
      |     ^
[DEBUG] ../common/pek/Types.h:52:5: warning: switch has identical branches [bugprone-branch-clone]
../common/pek/File.h:10:1: error: example hard error [clang-diagnostic-error]
../common/pek/File.h:11:1: note: notes should not be counted [clang-diagnostic-note]
"""


class ClangTidyStatisticsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        runtime_probe = subprocess.run(
            [
                sys.executable,
                "-c",
                "import argcomplete, git; from expkits_ci.quality_checks import QualityChecks; from expkits_ci.expkits_ci import enable_implicit_verbose_logging",
            ],
            env={**os.environ, "PYTHONPATH": str(PACKAGE_ROOT)},
            capture_output=True,
            text=True,
        )
        if runtime_probe.returncode != 0:
            raise unittest.SkipTest(
                "expkits-ci runtime is unavailable; run with a venv containing argcomplete and GitPython."
            )

        from expkits_ci.quality_checks import QualityChecks as ImportedQualityChecks
        from expkits_ci import expkits_ci as ImportedCli
        cls.quality_checks = ImportedQualityChecks
        cls.cli = ImportedCli

    def parse_args(self, *args):
        parser = argparse.ArgumentParser()
        self.cli.setup_argument_parser(parser)
        parsed_args = parser.parse_args(list(args))
        self.cli.enable_implicit_verbose_logging(parsed_args)
        return parsed_args

    def setUp(self):
        self.work_dir = tempfile.TemporaryDirectory()
        self.work_path = Path(self.work_dir.name)

    def tearDown(self):
        self.work_dir.cleanup()

    def write_json(self, relative_path, payload):
        target = self.work_path / relative_path
        target.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        return target

    def test_parse_clang_tidy_statistics_counts_checks_and_severities(self):
        log_file = self.work_path / "clang-tidy.log"
        log_file.write_text(CLANG_TIDY_LOG, encoding="utf-8")

        check_counts, severity_counts = self.quality_checks.parse_clang_tidy_statistics(str(log_file))

        self.assertEqual(check_counts["modernize-avoid-c-arrays"], 2)
        self.assertEqual(check_counts["bugprone-branch-clone"], 1)
        self.assertEqual(check_counts["clang-diagnostic-error"], 1)
        self.assertNotIn("clang-diagnostic-note", check_counts)
        self.assertEqual(severity_counts["warning"], 3)
        self.assertEqual(severity_counts["error"], 1)

    def test_parse_clang_tidy_diagnostics_preserves_source_location_and_context(self):
        log_file = self.work_path / "clang-tidy.log"
        log_file.write_text(CLANG_TIDY_LOG, encoding="utf-8")

        diagnostics = self.quality_checks.parse_clang_tidy_diagnostics(str(log_file))

        first_diagnostic = diagnostics[0]
        self.assertEqual(first_diagnostic["path"], "../common/pek/Shape.h")
        self.assertEqual(first_diagnostic["line"], 33)
        self.assertEqual(first_diagnostic["column"], 9)
        self.assertEqual(first_diagnostic["severity"], "warning")
        self.assertEqual(first_diagnostic["message"], "do not declare C-style arrays")
        self.assertEqual(first_diagnostic["check"], "modernize-avoid-c-arrays")
        self.assertEqual(first_diagnostic["source_context"], [
            "   33 |     int temporary[] = {1, 2};",
            "      |     ^",
        ])

    def test_enforce_baseline_fails_when_any_check_regresses(self):
        log_file = self.work_path / "clang-tidy.log"
        log_file.write_text(CLANG_TIDY_LOG, encoding="utf-8")
        baseline_file = self.write_json("baseline.json", {
            "checks": {
                "modernize-avoid-c-arrays": 1,
                "bugprone-branch-clone": 1,
            }
        })
        stats = {
            "checks": {
                "modernize-avoid-c-arrays": 2,
                "bugprone-branch-clone": 1,
            }
        }
        diagnostics = self.quality_checks.parse_clang_tidy_diagnostics(str(log_file))

        with self.assertLogs("expkits_ci", level="ERROR") as logs:
            result = self.quality_checks.compare_clang_tidy_statistics_to_baseline(
                stats, str(baseline_file), mode="enforce", diagnostics=diagnostics)

        self.assertFalse(result)
        output = "\n".join(logs.output)
        self.assertIn(
            "baseline stores counts only, so it cannot identify which +1 diagnostic(s) are new",
            output,
        )
        self.assertIn(
            "../common/pek/Shape.h:33:9: warning: do not declare C-style arrays "
            "[modernize-avoid-c-arrays]",
            output,
        )
        self.assertIn("33 |     int temporary[] = {1, 2};", output)

    def test_zero_baseline_regression_connects_diff_to_exact_diagnostic(self):
        log_file = self.work_path / "clang-tidy.log"
        log_file.write_text(CLANG_TIDY_LOG, encoding="utf-8")
        baseline_file = self.write_json("baseline.json", {"checks": {}})

        with self.assertLogs("expkits_ci", level="ERROR") as logs:
            result = self.quality_checks.report_clang_tidy_statistics(
                str(log_file),
                baseline_file=str(baseline_file),
                baseline_mode="enforce",
            )

        self.assertFalse(result)
        output = "\n".join(logs.output)
        self.assertIn(
            "modernize-avoid-c-arrays: the following 2 diagnostic(s) account for the +2 regression",
            output,
        )
        self.assertIn("98 |     int dims[8] = {0};", output)

    def test_update_baseline_refuses_regressions_and_leaves_file_unchanged(self):
        log_file = self.work_path / "clang-tidy.log"
        log_file.write_text(CLANG_TIDY_LOG, encoding="utf-8")
        initial_baseline = {
            "checks": {
                "modernize-avoid-c-arrays": 1,
                "bugprone-branch-clone": 2,
            }
        }
        baseline_file = self.write_json("baseline.json", initial_baseline)
        before = baseline_file.read_text(encoding="utf-8")

        result = self.quality_checks.report_clang_tidy_statistics(
            str(log_file),
            baseline_file=str(baseline_file),
            update_baseline=True,
        )

        self.assertFalse(result)
        self.assertEqual(baseline_file.read_text(encoding="utf-8"), before)

    def test_clang_tidy_file_logging_enables_verbose_diagnostics(self):
        file_args = self.parse_args("--clang-tidy", "--log-output", "file", "--log-file", "clang-tidy.log")
        both_args = self.parse_args("--clang-tidy", "--log-output", "both", "--log-file", "clang-tidy.log")
        stdout_args = self.parse_args("--clang-tidy", "--log-file", "clang-tidy.log")

        self.assertTrue(file_args.verbose)
        self.assertTrue(both_args.verbose)
        self.assertFalse(stdout_args.verbose)


if __name__ == "__main__":
    unittest.main()
