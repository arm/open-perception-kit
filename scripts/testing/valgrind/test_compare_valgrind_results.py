#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import importlib.util
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

            self.assertEqual(compare.load_summary(current) - compare.load_summary(baseline), set())

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
        self.assertIn("xwhat=8 bytes lost", next(iter(new_errors)))

    def test_changed_unsymbolized_ip_creates_new_error(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            baseline = tmp / "baseline.xml"
            current = tmp / "current.xml"
            self.write_summary(baseline, self.unsymbolized_error_xml(ip="0x123456"))
            self.write_summary(current, self.unsymbolized_error_xml(ip="0xabcdef"))

            new_errors = compare.load_summary(current) - compare.load_summary(baseline)

        self.assertEqual(len(new_errors), 1)
        self.assertIn("ip=0xabcdef", next(iter(new_errors)))

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
    def unsymbolized_error_xml(ip: str) -> str:
        return f"""
          <error>
            <kind>Leak_DefinitelyLost</kind>
            <xwhat><text>4 bytes lost</text></xwhat>
            <stack>
              <frame>
                <ip>{ip}</ip>
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


if __name__ == "__main__":
    unittest.main()
