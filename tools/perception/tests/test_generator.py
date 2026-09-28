# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from __future__ import annotations

import hashlib
import importlib
import io
import shutil
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from dataclasses import replace
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/flowdata-sdk/tools/flowdata"))
app = importlib.import_module("engine.app")
discovery = importlib.import_module("engine.discovery")
compat = importlib.import_module("engine.flatbuffers_compat")
validation = importlib.import_module("engine.validation")
verify_generation_manifest = importlib.import_module("engine.manifest").verify_generation_manifest
engine_types = importlib.import_module("engine.types")
RESERVED_PAYLOAD_ID_MIN = engine_types.RESERVED_PAYLOAD_ID_MIN
SemanticVersion = engine_types.SemanticVersion

PAYLOAD = 'namespace sample; table Payload {} root_type Payload; file_identifier "TEST";'


class GeneratorTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory(prefix="flowdata-generator-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.schema = self.root / "payload.fbs"

    def test_generate_all_sdks_in_process_and_relocate_inputs(self) -> None:
        schemas = self.root / "schemas"
        relocated = self.root / "relocated"
        shutil.copytree(ROOT / "schemas/perception/metadata", schemas)
        shutil.copytree(schemas, relocated)
        output = self.root / "generated"
        expected = {
            "cpp": ["sample.h", "cmake/sample.cmake", "meson/sample/meson.build"],
            "python": ["pyproject.toml", "src/sample/packet.py", "src/sample/guest.pyi"],
            "rust": ["Cargo.toml", "src/lib.rs"],
            "ts": ["package.json", "src/sample/index.ts"],
        }
        for sdk, files in expected.items():
            with self.subTest(sdk=sdk), redirect_stdout(io.StringIO()):
                options = ["--cmake", "--meson", "--cpp-python-bridge"] if sdk == "cpp" else []
                self.assertEqual(app.main([
                    "generate", "--name", "sample", "--version", "1.2.3", "--sdk", sdk,
                    "--schema-dir", str(schemas), "--generated-root", str(output), *options,
                ]), 0)
                target = output / sdk
                for file in files:
                    self.assertTrue((target / file).is_file(), file)
                manifest = target / "flowdata-manifest.json"
                receipt = verify_generation_manifest(manifest, schemas)
                self.assertEqual(receipt["outputs"]["sdk"], sdk)
                self.assertEqual(len(receipt["payloads"]), 9)
                before = {
                    path.relative_to(target): hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in target.rglob("*") if path.is_file()
                }
                context = app._build_context(
                    "sample", SemanticVersion(1, 2, 3), relocated, output, "flatc", sdk == "cpp"
                )
                integrations = ["cmake", "meson"] if sdk == "cpp" else []
                self.assertEqual(app._run_generate(context, sdk, integrations), 0)
                after = {
                    path.relative_to(target): hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in target.rglob("*") if path.is_file()
                }
                self.assertEqual(before, after)
                self.assertEqual(app.main([
                    "verify-manifest", str(manifest), "--schema-root", str(relocated)
                ]), 0)

    def test_cli_arguments(self) -> None:
        self.assertEqual(app._sdk_name_arg("sdk_123"), "sdk_123")
        self.assertEqual(str(app._semantic_version_arg("0.12.3")), "0.12.3")
        arguments = ["generate", "--name", "sample", "--version", "1.2.3", "--schema-dir", "."]
        for option, values in {
            "--name": ["", "Upper", "bad-name", "9name", "caf\u00e9", "class", "async", "crate"],
            "--version": ["1.2", "01.2.3", "1.2.3-rc1", "1.2.3\n", "\u0661.2.3"],
            "--sdk": ["java"],
        }.items():
            for value in values:
                with self.subTest(option=option, value=value), redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit) as error:
                        app.main([*arguments, option, value])
                    self.assertEqual(error.exception.code, 2)
        with redirect_stdout(io.StringIO()) as output, self.assertRaises(SystemExit) as error:
            app.main(["--version"])
        self.assertEqual(error.exception.code, 0)
        self.assertIn("sdkgen", output.getvalue())

    def test_cpp_options_rejected_for_other_sdks(self) -> None:
        context = app._build_context("sample", SemanticVersion(1, 0, 0), self.root, self.root, "flatc")
        for sdk in ("python", "rust", "ts"):
            with self.subTest(sdk=sdk):
                with self.assertRaisesRegex(SystemExit, "integrations are supported only"):
                    app._run_generate(context, sdk, ["meson"])
                with self.assertRaisesRegex(SystemExit, "bridge is supported only"):
                    app._run_generate(replace(context, cpp_python_bridge=True), sdk, [])

    def test_strip_comments_preserves_strings_and_layout(self) -> None:
        cases = [
            ("", ""), ("/", "/"), ("//", "  "), ("/**/", "    "),
            ("a//text\r\nb", "a      \r\nb"),
            ("a/*x\r\ny*/b", "a   \n\n   b"),
            ("/*unterminated\r", "              \n"),
            ('"unterminated//', '"unterminated//'),
            ('"ends with \\', '"ends with \\'),
            (r'"escaped\" // /* quote"', r'"escaped\" // /* quote"'),
            (r'"backslash\\"//x', '"backslash\\\\"   '),
            ('"http://host/*path*/"/*x*/', '"http://host/*path*/"     '),
            ("/* // \" */root_type P;", "          root_type P;"),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.assertEqual(discovery._strip_comments(source), expected)

    def test_discovery_roots_and_errors(self) -> None:
        cases = [
            (PAYLOAD, "sample.Payload"),
            (PAYLOAD.replace("root_type Payload", "namespace other; root_type sample.Payload"), "sample.Payload"),
            (PAYLOAD.replace("root_type Payload", "root_type sample::Payload"), "sample.Payload"),
            ("namespace sample; table Helper {}", None),
        ]
        for text, expected in cases:
            self.schema.write_text(text, encoding="utf-8")
            entry = discovery._parse_schema_entry(self.schema)
            self.assertEqual(entry.qualified_root_type if entry else None, expected)
        for text, message in [
            (PAYLOAD + " root_type Payload;", "expected zero or one root_type"),
            ('root_type Payload;', "no active FlatBuffers namespace"),
            ('root_type sample.Payload;', "missing a FlatBuffers namespace declaration"),
            (PAYLOAD.split("file_identifier")[0], "missing a FlatBuffers file_identifier"),
            (PAYLOAD.replace('"TEST"', '"BAD"'), "exactly 4 characters"),
        ]:
            with self.subTest(message=message):
                self.schema.write_text(text, encoding="utf-8")
                with self.assertRaisesRegex(SystemExit, message) as error:
                    discovery._parse_schema_entry(self.schema)
                self.assertIn("Example:", str(error.exception))
                self.assertIn("Fix:", str(error.exception))

    def test_schema_directory_errors(self) -> None:
        for path, message in [(self.root / "missing", "does not exist"), (self.root, "no .fbs files")]:
            with self.assertRaisesRegex(SystemExit, message):
                discovery.discover_schema_set(path)
        self.schema.write_text("table Helper {}", encoding="utf-8")
        with self.assertRaisesRegex(SystemExit, "expected a schema directory"):
            discovery.discover_schema_set(self.schema)
        with self.assertRaisesRegex(SystemExit, "no payload root schemas"):
            discovery.discover_schema_set(self.root)
        nested = self.root / "nested"
        nested.mkdir()
        (nested / self.schema.name).write_text(PAYLOAD, encoding="utf-8")
        with self.assertRaisesRegex(SystemExit, "duplicate FlatBuffers schema basename"):
            discovery.discover_schema_set(self.root)

    def test_entry_validation(self) -> None:
        self.schema.write_text(PAYLOAD, encoding="utf-8")
        entry = replace(discovery._parse_schema_entry(self.schema), numeric_id=1)
        other = replace(entry, numeric_id=2, file_identifier="OTHR", qualified_root_type="sample.Other",
                        native_type="sample::OtherT", table_type="sample::Other")
        validation.validate_entries([entry, other], "sample")
        for field, message in [
            ("numeric_id", "generated payload id collision"), ("file_identifier", "duplicate file_identifier"),
            ("qualified_root_type", "duplicate payload root type"), ("native_type", "duplicate native type"),
            ("table_type", "duplicate table type"),
        ]:
            with self.subTest(field=field), self.assertRaisesRegex(SystemExit, message):
                validation.validate_entries([entry, replace(other, **{field: getattr(entry, field)})], "sample")
        with self.assertRaisesRegex(SystemExit, "is reserved"):
            validation.validate_entries([replace(entry, numeric_id=RESERVED_PAYLOAD_ID_MIN)], "sample")
        for namespace in ("sample::internalfb", "sample::internalfb::child"):
            with self.assertRaisesRegex(SystemExit, "reserved for the generated"):
                validation.validate_entries([replace(entry, namespace=namespace)], "sample")

    def test_schema_namespace_validation(self) -> None:
        for namespace in ("sample.internalfb", "sample.internalfb.child", "flatbuffers", "np.child", "typing"):
            with self.subTest(namespace=namespace):
                self.schema.write_text(f"namespace {namespace};", encoding="utf-8")
                with self.assertRaisesRegex(SystemExit, "reserved|conflicts"):
                    validation.validate_schema_namespaces([self.schema], "sample")
        self.schema.write_text('/* namespace np; */ namespace sample::internalfb_extra;', encoding="utf-8")
        validation.validate_schema_namespaces([self.schema], "sample")

    def test_flatbuffers_compatibility_and_runtime_mapping(self) -> None:
        for text, supported in [("24.3.24", False), ("24.3.25", True), ("25.9.23", True), ("26.0.0", False)]:
            self.assertEqual(compat.is_supported_flatbuffers_version(compat.parse_flatbuffers_version(text)), supported)
        with self.assertRaisesRegex(ValueError, "cannot parse"):
            compat.parse_flatbuffers_version("flatc development")
        version = SemanticVersion(25, 9, 23)
        for sdk, languages in [("cpp", ["cpp"]), ("python", ["python"]), ("ts", ["typescript"]), ("rust", ["rust"])]:
            contracts = compat.flatbuffers_runtime_contracts(sdk, False, version)
            self.assertEqual([item["language"] for item in contracts], languages)
            expected = "==25.9.23" if sdk in ("cpp", "rust") else ">=24.3.25,<26.0.0"
            self.assertEqual(contracts[0]["version_requirement"], expected)
        contracts = compat.flatbuffers_runtime_contracts("cpp", True, version)
        self.assertEqual([item["language"] for item in contracts], ["cpp", "python"])
        with self.assertRaisesRegex(ValueError, "unsupported SDK"):
            compat.flatbuffers_runtime_contracts("java", False, version)

    def test_flatc_errors(self) -> None:
        for output, message in [
            ("", "returned no version"), ("flatc 26.0.0", "unsupported flatc"), ("unknown", "cannot parse")
        ]:
            with patch.object(compat.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, output, "")):
                with self.assertRaisesRegex(ValueError, message):
                    compat.require_supported_flatc("flatc")
        for failure, message in [
            (FileNotFoundError(), "not found"),
            (subprocess.CalledProcessError(1, "flatc", stderr="broken"), "broken"),
        ]:
            with patch.object(compat.subprocess, "run", side_effect=failure):
                with self.assertRaisesRegex(SystemExit, message):
                    app._build_context("sample", SemanticVersion(1, 0, 0), self.root, self.root, "flatc")


if __name__ == "__main__":
    unittest.main()
