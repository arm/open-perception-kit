#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).with_name("summarize-valgrind-output.py")


def import_summary_module():
    spec = importlib.util.spec_from_file_location("summarize_valgrind_output", SCRIPT_PATH)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


summary = import_summary_module()


class TestSummarizeValgrindOutput(unittest.TestCase):
    def test_collect_errors_deduplicates_normalized_errors(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            logs_dir = Path(tmpdir)
            output = logs_dir / "summary.valgrind.output.xml"
            self.write_log(
                logs_dir / "pipeline.valgrind.1.xml",
                """
                <valgrindoutput>
                  <status><state>RUNNING</state></status>
                  <status><state>FINISHED</state></status>
                  <error>
                    <unique>0x1</unique>
                    <tid>1</tid>
                    <kind>Leak_DefinitelyLost</kind>
                    <xwhat>
                      <text>5 bytes lost in loss record 1 of 20</text>
                      <leakedbytes>5</leakedbytes>
                      <leakedblocks>1</leakedblocks>
                    </xwhat>
                    <stack>
                      <frame>
                        <ip>0x4844818</ip>
                        <obj>/usr/lib/libexample.so</obj>
                        <fn>malloc</fn>
                      </frame>
                      <frame>
                        <ip>0x5F537B3</ip>
                        <obj>/work/development/build/meson-out/libpekinfer.so</obj>
                        <fn>gst_pekinfer_init(_GstPekInfer*)</fn>
                        <file>pekinfer.cpp</file>
                        <line>394</line>
                      </frame>
                    </stack>
                  </error>
                  <error>
                    <unique>0x2</unique>
                    <tid>7</tid>
                    <kind>Leak_DefinitelyLost</kind>
                    <xwhat>
                      <text>5 bytes lost in loss record 9 of 100</text>
                      <leakedbytes>5</leakedbytes>
                      <leakedblocks>1</leakedblocks>
                    </xwhat>
                    <stack>
                      <frame>
                        <ip>0xABCDEF</ip>
                        <obj>/usr/lib/libexample.so</obj>
                        <fn>malloc</fn>
                      </frame>
                      <frame>
                        <ip>0x123456</ip>
                        <obj>/work/development/build/meson-out/libpekinfer.so</obj>
                        <fn>gst_pekinfer_init(_GstPekInfer*)</fn>
                        <file>pekinfer.cpp</file>
                        <line>394</line>
                      </frame>
                    </stack>
                  </error>
                </valgrindoutput>
                """,
            )
            self.write_log(
                logs_dir / "pipeline.valgrind.2.xml",
                """
                <valgrindoutput>
                  <status><state>RUNNING</state></status>
                  <status><state>FINISHED</state></status>
                  <error>
                    <unique>0x3</unique>
                    <tid>1</tid>
                    <kind>Leak_DefinitelyLost</kind>
                    <xwhat>
                      <text>5 bytes lost in loss record 12 of 250</text>
                      <leakedbytes>5</leakedbytes>
                      <leakedblocks>1</leakedblocks>
                    </xwhat>
                    <stack>
                      <frame>
                        <ip>0x555555</ip>
                        <obj>/usr/lib/libexample.so</obj>
                        <fn>malloc</fn>
                      </frame>
                      <frame>
                        <ip>0x777777</ip>
                        <obj>/work/development/build/meson-out/libpekinfer.so</obj>
                        <fn>gst_pekinfer_init(_GstPekInfer*)</fn>
                        <file>pekinfer.cpp</file>
                        <line>394</line>
                      </frame>
                    </stack>
                  </error>
                  <error>
                    <unique>0x4</unique>
                    <tid>1</tid>
                    <kind>Leak_DefinitelyLost</kind>
                    <xwhat>
                      <text>8 bytes lost in loss record 14 of 250</text>
                      <leakedbytes>8</leakedbytes>
                      <leakedblocks>1</leakedblocks>
                    </xwhat>
                    <stack>
                      <frame>
                        <ip>0x999999</ip>
                        <obj>/usr/lib/libexample.so</obj>
                        <fn>malloc</fn>
                      </frame>
                      <frame>
                        <ip>0x888888</ip>
                        <obj>/work/development/build/meson-out/libpekinfer.so</obj>
                        <fn>gst_pekinfer_set_property(_GObject*)</fn>
                        <file>pekinfer.cpp</file>
                        <line>272</line>
                      </frame>
                    </stack>
                  </error>
                </valgrindoutput>
                """,
            )

            root = summary.collect_errors(logs_dir, output)

        errors = root.findall("error")
        self.assertEqual(root.get("source_logs"), "2")
        self.assertEqual(root.get("raw_errors"), "4")
        self.assertEqual(root.get("duplicate_errors"), "2")
        self.assertEqual(root.get("collected_errors"), "2")
        self.assertEqual(len(errors), 2)
        self.assertEqual(errors[0].findtext("unique"), "0x1")
        self.assertEqual(errors[0].findtext("tid"), "THREAD")
        self.assertEqual(errors[0].findtext("stack/frame/ip"), "0xADDR")
        self.assertEqual(
            errors[0].findtext("xwhat/text"),
            "5 bytes lost in loss record N of N",
        )
        self.assertEqual(errors[1].findtext("xwhat/leakedbytes"), "8")

    def test_source_logs_counts_complete_logs_without_errors(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            logs_dir = Path(tmpdir)
            self.write_log(
                logs_dir / "clean.valgrind.1.xml",
                """
                <valgrindoutput>
                  <status><state>RUNNING</state></status>
                  <status><state>FINISHED</state></status>
                </valgrindoutput>
                """,
            )
            self.write_log(
                logs_dir / "error.valgrind.2.xml",
                """
                <valgrindoutput>
                  <status><state>RUNNING</state></status>
                  <status><state>FINISHED</state></status>
                  <error>
                    <unique>0x1</unique>
                    <tid>4</tid>
                    <kind>InvalidRead</kind>
                    <what>Invalid read of size 4 at 0x1234</what>
                  </error>
                </valgrindoutput>
                """,
            )

            root = summary.collect_errors(logs_dir, logs_dir / "summary.xml")

        self.assertEqual(root.get("source_logs"), "2")
        self.assertEqual(root.get("raw_errors"), "1")
        self.assertEqual(root.get("collected_errors"), "1")
        self.assertEqual(root.findtext("error/tid"), "THREAD")
        self.assertEqual(root.findtext("error/what"), "Invalid read of size 4 at 0xADDR")

    def test_collect_errors_rejects_incomplete_valgrind_xml(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            logs_dir = Path(tmpdir)
            self.write_log(
                logs_dir / "incomplete.valgrind.1.xml",
                """
                <valgrindoutput>
                  <status><state>RUNNING</state></status>
                  <error><kind>InvalidRead</kind></error>
                </valgrindoutput>
                """,
            )

            with self.assertRaisesRegex(ValueError, "final status is 'RUNNING'"):
                summary.collect_errors(logs_dir, logs_dir / "summary.xml")

    def test_collect_errors_rejects_unexpected_xml_root(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            logs_dir = Path(tmpdir)
            self.write_log(
                logs_dir / "badroot.valgrind.1.xml",
                """
                <error>
                  <kind>InvalidRead</kind>
                </error>
                """,
            )

            with self.assertRaisesRegex(ValueError, "unexpected Valgrind XML root"):
                summary.collect_errors(logs_dir, logs_dir / "summary.xml")

    def test_unsymbolized_frame_ip_differentiates_fingerprints(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            logs_dir = Path(tmpdir)
            self.write_log(
                logs_dir / "unsymbolized.valgrind.1.xml",
                """
                <valgrindoutput>
                  <status><state>RUNNING</state></status>
                  <status><state>FINISHED</state></status>
                  <error>
                    <unique>0x1</unique>
                    <tid>1</tid>
                    <kind>InvalidRead</kind>
                    <stack>
                      <frame>
                        <ip>0x1000</ip>
                        <obj>/usr/lib/libexample.so</obj>
                      </frame>
                    </stack>
                  </error>
                  <error>
                    <unique>0x2</unique>
                    <tid>1</tid>
                    <kind>InvalidRead</kind>
                    <stack>
                      <frame>
                        <ip>0x2000</ip>
                        <obj>/usr/lib/libexample.so</obj>
                      </frame>
                    </stack>
                  </error>
                </valgrindoutput>
                """,
            )

            root = summary.collect_errors(logs_dir, logs_dir / "summary.xml")

        errors = root.findall("error")
        self.assertEqual(root.get("raw_errors"), "2")
        self.assertEqual(root.get("duplicate_errors"), "0")
        self.assertEqual(root.get("collected_errors"), "2")
        self.assertEqual(
            [error.findtext("stack/frame/ip") for error in errors],
            ["0x1000", "0x2000"],
        )

    def test_symbolized_frame_ip_does_not_split_fingerprints(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            logs_dir = Path(tmpdir)
            self.write_log(
                logs_dir / "symbolized.valgrind.1.xml",
                """
                <valgrindoutput>
                  <status><state>RUNNING</state></status>
                  <status><state>FINISHED</state></status>
                  <error>
                    <unique>0x1</unique>
                    <tid>1</tid>
                    <kind>InvalidRead</kind>
                    <stack>
                      <frame>
                        <ip>0x1000</ip>
                        <obj>/usr/lib/libexample.so</obj>
                        <fn>example</fn>
                        <file>example.cpp</file>
                        <line>42</line>
                      </frame>
                    </stack>
                  </error>
                  <error>
                    <unique>0x2</unique>
                    <tid>7</tid>
                    <kind>InvalidRead</kind>
                    <stack>
                      <frame>
                        <ip>0x2000</ip>
                        <obj>/usr/lib/libexample.so</obj>
                        <fn>example</fn>
                        <file>example.cpp</file>
                        <line>42</line>
                      </frame>
                    </stack>
                  </error>
                </valgrindoutput>
                """,
            )

            root = summary.collect_errors(logs_dir, logs_dir / "summary.xml")

        self.assertEqual(root.get("raw_errors"), "2")
        self.assertEqual(root.get("duplicate_errors"), "1")
        self.assertEqual(root.get("collected_errors"), "1")
        self.assertEqual(root.findtext("error/stack/frame/ip"), "0xADDR")

    @staticmethod
    def write_log(path: Path, content: str) -> None:
        path.write_text(content, encoding="utf-8")


if __name__ == "__main__":
    unittest.main()
