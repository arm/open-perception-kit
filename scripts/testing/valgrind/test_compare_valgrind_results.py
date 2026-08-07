#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import hashlib
import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).with_name("compare-valgrind-results.py")
VALGRIND_DRIVER_PATH = Path(__file__).with_name("test-elements-with-valgrind.sh")
EXPECTED_SUPPRESSION_MANIFEST_SHA256 = (
    "38649edbb8e72237c286d43162439d19a7b576e934d0831c90b8533c675981c1"  # pragma: allowlist secret
)
PEK_SUPPRESSION_FUNCTIONS = {
    "_Z21gst_pek_comm_get_typev",
    "_Z21gst_pek_sink_get_typev",
    "_ZL18pekosd_plugin_initP10_GstPlugin",
    "_ZL19pekcomm_plugin_initP10_GstPlugin",
    "_ZL19peksink_plugin_initP10_GstPlugin",
    "_ZL20pekinfer_plugin_initP10_GstPlugin",
    "_ZL22gst_pek_osd_class_initP15_GstPekOsdClass",
    "_ZL22pektracker_plugin_initP10_GstPlugin",
    "_ZL23gst_pek_comm_class_initP15GstPekCommClass",
    "_ZL23gst_pek_sink_class_initP16_GstPekSinkClass",
    "_ZL23gst_pekinfer_class_initP16GstPekInferClass",
    "_ZL25gst_pekinfer_transform_ipP17_GstBaseTransformP10_GstBuffer",
    "_ZL25gst_pek_osd_get_type_oncev",
    "_ZL25gst_pektracker_class_initP18GstPekTrackerClass",
    "_ZL26gst_pek_comm_get_type_oncev",
    "_ZL26gst_pek_sink_get_type_oncev",
    "_ZL26gst_pekinfer_get_type_oncev",
    "_ZL26pekperformance_plugin_initP10_GstPlugin",
    "_ZL28gst_pek_comm_method_get_typev",
    "_ZL28gst_pektracker_get_type_oncev",
    "_ZL29gst_pek_osd_class_intern_initPv",
    "_ZL30gst_pek_comm_class_intern_initPv",
    "_ZL30gst_pek_performance_class_initP23_GstPekPerformanceClass",
    "_ZL30gst_pek_sink_class_intern_initPv",
    "_ZL30gst_pekinfer_class_intern_initPv",
    "_ZL32gst_pektracker_class_intern_initPv",
    "_ZL33gst_pek_performance_get_type_oncev",
    "_ZL37gst_pek_performance_class_intern_initPv",
    "_ZN3pek4MetaINS_20PerceptionMetaTraitsEE3addEP10_GstBufferSt10shared_ptrINS_10PerceptionEE",  # pragma: allowlist secret
    "_ZN3pek4MetaINS_20PerceptionMetaTraitsEE3getEP10_GstBuffer",  # pragma: allowlist secret
    "_ZN3pek4MetaINS_20PerceptionMetaTraitsEE4infoEv",  # pragma: allowlist secret
    "_ZN3pek4MetaINS_20PerceptionMetaTraitsEE8api_typeEv",  # pragma: allowlist secret
    "_ZN3pek4onnx11InferenceOp9configureERKNS_12AttributeMapE",  # pragma: allowlist secret
    "_ZN3pek4onnx9Inference13setupFromJsonERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE",  # pragma: allowlist secret
    "_ZN3pek4onnx9Inference5setupERKNS_15ModelDescriptorE",  # pragma: allowlist secret
    "_ZN3pek5Tools18DynamicLibraryOpenERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE",  # pragma: allowlist secret
    "gst_pek_osd_get_type",
    "gst_pek_performance_get_type",
    "gst_pekinfer_get_type",
    "gst_pektracker_get_type",
}


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

    def test_suppressions_match_only_the_approved_full_stack_manifest(self):
        driver = VALGRIND_DRIVER_PATH.read_text(encoding="utf-8")
        self.assertEqual(
            [line.strip() for line in driver.splitlines() if "--num-callers=" in line],
            ["--num-callers=64"],
        )

        suppressions = SCRIPT_PATH.with_name("suppressed-warnings").read_text(encoding="utf-8")
        blocks = []
        block = None
        for line in suppressions.splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if line == "{":
                self.assertIsNone(block)
                block = []
            elif line == "}":
                self.assertIsNotNone(block)
                blocks.append(tuple(block))
                block = None
            else:
                self.assertIsNotNone(block)
                block.append(line)
        self.assertIsNone(block)

        self.assertEqual(len(blocks), 260)
        self.assertTrue(
            {
                "gstreamer_registry_or_plugin_loader_reachable",
                "onnxruntime_pthread_once_small_definite",
                "ld_loader_dlopen_reachable_generic_ld",
            }.isdisjoint(block[0] for block in blocks)
        )
        manifest = "\n\n".join("\n".join(block) for block in sorted(blocks))
        self.assertEqual(
            hashlib.sha256(manifest.encode()).hexdigest(),
            EXPECTED_SUPPRESSION_MANIFEST_SHA256,
        )

        sequences = set()
        repository_functions = set()
        for name, tool, leak_kinds, *frames in blocks:
            self.assertTrue(name.startswith("pek_reachable_"))
            self.assertRegex(name, r"\A[a-z_]+\Z")
            self.assertEqual(tool, "Memcheck:Leak")
            self.assertEqual(leak_kinds, "match-leak-kinds: reachable")
            self.assertTrue(frames)
            self.assertLess(len(frames), 64)
            self.assertNotIn("...", frames)
            self.assertIn(frames[-1], {"fun:(below main)", "fun:clone"})
            if frames[-1] == "fun:clone":
                self.assertEqual(frames[-2], "fun:start_thread")

            sequence = tuple(frames)
            self.assertNotIn(sequence, sequences)
            sequences.add(sequence)

            anchors = {
                frame.removeprefix("fun:")
                for frame in frames
                if frame.startswith("fun:")
                and frame.removeprefix("fun:") in PEK_SUPPRESSION_FUNCTIONS
            }
            self.assertTrue(anchors)
            repository_functions.update(anchors)
            self.assertTrue(
                any(
                    frame.startswith("obj:/usr/")
                    or frame.startswith("obj:/opt/")
                    or (
                        frame.startswith("fun:")
                        and frame.removeprefix("fun:") not in PEK_SUPPRESSION_FUNCTIONS
                    )
                    for frame in frames
                )
            )

            for frame in frames:
                self.assertTrue(frame.startswith(("fun:", "obj:")))
                self.assertFalse(frame.startswith("src:"))
                if frame.startswith("fun:"):
                    self.assertNotIn("*", frame)
                    self.assertNotIn("?", frame)
                elif "*" in frame or "?" in frame:
                    self.assertNotIn("obj:/work/", frame)
                    self.assertTrue(frame.endswith(".so.*"))
                    self.assertEqual(frame.count("*"), 1)
                    self.assertNotIn("?", frame)

        self.assertEqual(repository_functions, PEK_SUPPRESSION_FUNCTIONS)

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

    def test_cli_ignores_third_party_only_errors_and_keeps_mixed_errors(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            baseline = tmp / "baseline.xml"
            current = tmp / "current.xml"
            self.write_summary(
                baseline,
                """
                <error>
                  <kind>Leak_PossiblyLost</kind>
                  <xwhat><text>16 bytes possibly lost</text></xwhat>
                  <stack>
                    <frame><obj>/usr/lib/libglib-2.0.so.0</obj></frame>
                  </stack>
                </error>
                """,
            )
            current_error = """
                <error>
                  <kind>Leak_DefinitelyLost</kind>
                  <xwhat><text>32768 bytes lost</text></xwhat>
                  <stack>
                    <frame><obj>/usr/bin/valgrind</obj></frame>
                    <frame><obj>/usr/lib/libglib-2.0.so.0</obj></frame>
                    <frame><obj>/usr/lib/libgobject-2.0.so.0</obj></frame>
                    <frame><obj>/usr/lib/libgstreamer-1.0.so.0</obj></frame>
                    <frame><obj>/workspace/development/libexample.so</obj></frame>
                  </stack>
                </error>
            """
            self.write_summary(current, current_error)

            result = self.run_cli(baseline, current)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("Baseline errors : 0", result.stderr)
            self.assertIn("Current errors  : 0", result.stderr)
            self.assertIn("New errors      : 0", result.stderr)
            self.assertIn("Fixed errors    : 0", result.stderr)
            self.assertNotIn("New errors (absent in baseline, present in current):", result.stderr)

            repository_stack = """
                  <stack>
                    <frame><obj>/usr/lib/libc.so.6</obj></frame>
                    <frame><file>/work/development/example.cpp</file></frame>
                  </stack>
            """
            self.write_summary(
                current,
                current_error.replace("</error>", f"{repository_stack}</error>"),
            )

            result = self.run_cli(baseline, current)

        self.assertEqual(result.returncode, 1)
        self.assertIn("Baseline errors : 0", result.stderr)
        self.assertIn("Current errors  : 1", result.stderr)
        self.assertIn("New errors      : 1", result.stderr)
        self.assertIn("Fixed errors    : 0", result.stderr)
        self.assertIn(
            "FAILED: 1 new Valgrind error(s) introduced compared to the baseline.",
            result.stderr,
        )

    def test_cli_reports_repository_owned_still_reachable_errors(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            baseline = tmp / "baseline.xml"
            current = tmp / "current.xml"
            self.write_summary(baseline, "")
            reachable_error = self.error_xml().replace(
                "Leak_DefinitelyLost",
                "Leak_StillReachable",
            )
            self.write_summary(current, reachable_error)

            result = self.run_cli(baseline, current)

        self.assertEqual(result.returncode, 1)
        self.assertIn("Current errors  : 1", result.stderr)
        self.assertIn("New errors      : 1", result.stderr)
        self.assertIn("[NEW] Leak_StillReachable", result.stderr)

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
