#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).with_name("compare-valgrind-results.py")


def import_compare_module():
    spec = importlib.util.spec_from_file_location("compare_valgrind_results", SCRIPT_PATH)
    if spec is None or spec.loader is None:
        raise ImportError(f"Cannot load compare module from {SCRIPT_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


compare = import_compare_module()


class TestCompareValgrindResults(unittest.TestCase):
    def test_line_number_changes_do_not_create_new_error(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            baseline = tmp / "baseline.xml"
            current = tmp / "current.xml"
            self.write_summary(baseline, self.error_xml(line=10))
            self.write_summary(current, self.error_xml(line=42))

            self.assertFalse(compare.load_summary(current) - compare.load_summary(baseline))

    def test_changed_symbol_creates_new_error(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            baseline = tmp / "baseline.xml"
            current = tmp / "current.xml"
            self.write_summary(baseline, self.error_xml(function="old_fn"))
            self.write_summary(current, self.error_xml(function="new_fn"))

            new_errors = compare.load_summary(current) - compare.load_summary(baseline)

        self.assertEqual(len(new_errors), 1)
        self.assertIn("fn=new_fn", next(iter(new_errors)))

    def test_changed_xwhat_text_creates_new_error(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            baseline = tmp / "baseline.xml"
            current = tmp / "current.xml"
            self.write_summary(baseline, self.error_xml(xwhat="4 bytes lost"))
            self.write_summary(current, self.error_xml(xwhat="8 bytes lost"))

            new_errors = compare.load_summary(current) - compare.load_summary(baseline)

        self.assertEqual(len(new_errors), 1)
        self.assertIn("what=8 bytes lost", next(iter(new_errors)))

    def test_changed_what_text_creates_new_error(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            baseline = tmp / "baseline.xml"
            current = tmp / "current.xml"
            self.write_summary(baseline, self.what_error_xml(what="Invalid read of size 4"))
            self.write_summary(current, self.what_error_xml(what="Invalid read of size 8"))

            new_errors = compare.load_summary(current) - compare.load_summary(baseline)

        self.assertEqual(len(new_errors), 1)
        self.assertIn("what=Invalid read of size 8", next(iter(new_errors)))

    def test_changed_unsymbolized_ip_is_ignored_when_stack_shape_matches(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            baseline = tmp / "baseline.xml"
            current = tmp / "current.xml"
            self.write_summary(baseline, self.unsymbolized_error_xml(ip="0x123456"))
            self.write_summary(current, self.unsymbolized_error_xml(ip="0xabcdef"))

            self.assertFalse(compare.load_summary(current) - compare.load_summary(baseline))

    def test_added_matching_unsymbolized_error_count_creates_new_error(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            baseline = tmp / "baseline.xml"
            current = tmp / "current.xml"
            self.write_summary(baseline, self.unsymbolized_error_xml(ip="0x123456"))
            self.write_summary(
                current,
                "\n".join(
                    (
                        self.unsymbolized_error_xml(ip="0xabcdef"),
                        self.unsymbolized_error_xml(ip="0xfedcba"),
                    )
                ),
            )

            new_errors = compare.load_summary(current) - compare.load_summary(baseline)

        self.assertEqual(sum(new_errors.values()), 1)
        self.assertIn("stack=unsymbolized[0]", next(iter(new_errors)))

    def test_unsymbolized_frame_depth_creates_new_error(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            baseline = tmp / "baseline.xml"
            current = tmp / "current.xml"
            self.write_summary(
                baseline,
                self.unsymbolized_error_xml(frame_ips=("0x123456",)),
            )
            self.write_summary(
                current,
                self.unsymbolized_error_xml(frame_ips=("0xabcdef", "0xfedcba")),
            )

            new_errors = compare.load_summary(current) - compare.load_summary(baseline)

        self.assertEqual(len(new_errors), 1)
        self.assertIn("stack=unsymbolized[0]|unsymbolized[1]", next(iter(new_errors)))

    def test_changed_unsymbolized_object_creates_new_error(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            baseline = tmp / "baseline.xml"
            current = tmp / "current.xml"
            self.write_summary(
                baseline,
                self.unsymbolized_error_xml(obj="/work/development/build/meson-out/libold.so"),
            )
            self.write_summary(
                current,
                self.unsymbolized_error_xml(obj="/work/development/build/meson-out/libnew.so"),
            )

            new_errors = compare.load_summary(current) - compare.load_summary(baseline)

        self.assertEqual(len(new_errors), 1)
        self.assertIn("obj=/work/development/build/meson-out/libnew.so", next(iter(new_errors)))

    def test_shared_object_version_changes_do_not_create_new_error(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            baseline = tmp / "baseline.xml"
            current = tmp / "current.xml"
            self.write_summary(
                baseline,
                self.unsymbolized_error_xml(obj="/usr/lib/libexpat.so.1.10.2"),
            )
            self.write_summary(
                current,
                self.unsymbolized_error_xml(obj="/usr/lib/libexpat.so.1.12.2"),
            )

            self.assertFalse(compare.load_summary(current) - compare.load_summary(baseline))

    def test_suppressions_do_not_pin_shared_library_versions(self):
        suppressions = SCRIPT_PATH.with_name("suppressed-warnings").read_text(encoding="utf-8")

        for line in suppressions.splitlines():
            if line.strip().startswith("obj:"):
                self.assertNotIn(".so.", line)

    def test_non_valgrind_xml_exits_with_input_error(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            summary = Path(tmpdir) / "summary.xml"
            summary.write_text("<not-valgrindoutput />", encoding="utf-8")

            with self.assertRaises(SystemExit) as context:
                compare.load_summary(summary)

        self.assertEqual(context.exception.code, 2)

    def test_cli_exits_zero_when_no_new_errors(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            baseline = tmp / "baseline.xml"
            current = tmp / "current.xml"
            self.write_summary(baseline, self.error_xml(line=10))
            self.write_summary(current, self.error_xml(line=42))

            result = self.run_cli(baseline, current)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("PASSED: No new Valgrind errors compared to the baseline.", result.stderr)

    def test_cli_exits_one_and_reports_new_errors(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            baseline = tmp / "baseline.xml"
            current = tmp / "current.xml"
            self.write_summary(baseline, self.error_xml(function="old_fn"))
            self.write_summary(current, self.error_xml(function="new_fn"))

            result = self.run_cli(baseline, current)

        self.assertEqual(result.returncode, 1)
        self.assertIn("New errors      : 1", result.stderr)
        self.assertIn("FAILED: 1 new Valgrind error(s) introduced compared to the baseline.", result.stderr)
        self.assertIn("[NEW] Leak_DefinitelyLost", result.stderr)
        self.assertIn(
            "#0 new_fn at /work/development/elements/example.cpp:10 "
            "(/work/development/build/meson-out/libexample.so)",
            result.stderr,
        )

    def test_cli_exits_two_for_invalid_input(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            baseline = tmp / "baseline.xml"
            current = tmp / "current.xml"
            baseline.write_text("<not-valgrindoutput />", encoding="utf-8")
            self.write_summary(current, self.error_xml())

            result = self.run_cli(baseline, current)

        self.assertEqual(result.returncode, 2)
        self.assertIn("Error: unexpected XML root element", result.stderr)

    @staticmethod
    def error_xml(function="example_fn", line=10, xwhat="4 bytes lost") -> str:
        return f"""
          <error>
            <kind>Leak_DefinitelyLost</kind>
            <xwhat><text>{xwhat}</text></xwhat>
            <stack>
              <frame>
                <obj>/work/development/build/meson-out/libexample.so</obj>
                <fn>{function}</fn>
                <dir>/work/development/elements</dir>
                <file>example.cpp</file>
                <line>{line}</line>
              </frame>
            </stack>
          </error>
        """

    @staticmethod
    def unsymbolized_error_xml(
        ip: str = "0x123456",
        obj: str = "",
        frame_ips: tuple[str, ...] | None = None,
    ) -> str:
        frame_ips = frame_ips or (ip,)
        frames = "\n".join(
            f"""
              <frame>
                <ip>{frame_ip}</ip>
                {f"<obj>{obj}</obj>" if obj else ""}
              </frame>
            """
            for frame_ip in frame_ips
        )
        return f"""
          <error>
            <kind>Leak_DefinitelyLost</kind>
            <xwhat><text>4 bytes lost</text></xwhat>
            <stack>
              {frames}
            </stack>
          </error>
        """

    @staticmethod
    def what_error_xml(what: str) -> str:
        return f"""
          <error>
            <kind>InvalidRead</kind>
            <what>{what}</what>
            <stack>
              <frame>
                <fn>example_fn</fn>
                <file>example.cpp</file>
              </frame>
            </stack>
          </error>
        """

    @staticmethod
    def write_summary(path: Path, errors: str) -> None:
        path.write_text(
            f"""
            <valgrindoutput>
              {errors}
            </valgrindoutput>
            """,
            encoding="utf-8",
        )

    @staticmethod
    def run_cli(baseline: Path, current: Path) -> subprocess.CompletedProcess:
        return subprocess.run(
            [
                sys.executable,
                str(SCRIPT_PATH),
                "--baseline",
                str(baseline),
                "--current",
                str(current),
            ],
            check=False,
            text=True,
            capture_output=True,
        )


if __name__ == "__main__":
    unittest.main()
