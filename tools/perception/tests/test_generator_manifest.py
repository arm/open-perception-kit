################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import copy
import hashlib
import importlib
import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "flowdata-sdk" / "tools" / "flowdata"))
generator_manifest = importlib.import_module("engine.manifest")
python_codegen = importlib.import_module("engine.python.python_sdk_codegen")


class ManifestFixture:
    """Small raw manifest, independent of flatc and the manifest writer."""

    def __init__(
        self,
        root: Path,
        sdk: str = "cpp",
        bridge: bool = False,
        public_name: str = "example",
    ) -> None:
        self.sdk_root = root / "sdk"
        self.schema_root = root / "schemas"
        self.path = self.sdk_root / "flowdata-manifest.json"
        schema_files = []
        payloads = []
        schema_digest = hashlib.sha256()
        for name, identifier, payload_id in (
            ("Alpha", "ALPH", "0x0000000000000001"),
            ("Zulu", "ZULU", "0x7fffffffffffffff"),
        ):
            relative = f"{name.lower()}.fbs"
            content = (
                f"namespace example;\ntable {name} {{ value:int; }}\n"
                f'root_type {name};\nfile_identifier "{identifier}";\n'
            ).encode("utf-8")
            schema_files.append(self.create_file(self.schema_root, relative, content))
            encoded_path = relative.encode("utf-8")
            schema_digest.update(len(encoded_path).to_bytes(8, "big"))
            schema_digest.update(encoded_path)
            schema_digest.update(len(content).to_bytes(8, "big"))
            schema_digest.update(content)
            payloads.append(
                {
                    "file_identifier": identifier,
                    "payload_id": payload_id,
                    "qualified_root_type": f"example.{name}",
                    "schema": relative,
                }
            )

        language = "typescript" if sdk == "ts" else sdk
        requirement = "==25.9.23" if sdk in {"cpp", "rust"} else ">=24.3.25,<26.0.0"
        runtimes = [
            {"language": language, "package": "flatbuffers", "version_requirement": requirement}
        ]
        self.manifest = {
            "files": [self.create_file(self.sdk_root, "generated.txt", b"abc")],
            "flatc": {
                "semantic_version": "25.9.23",
                "version": "flatc version 25.9.23",
                "version_requirement": ">=24.3.25,<26.0.0",
            },
            "flatbuffers_runtimes": runtimes,
            "generator": {"name": "flowdata-sdk", "version": "0.6.0"},
            "outputs": {"cpp_python_bridge": bridge, "integrations": [], "sdk": sdk},
            "payloads": payloads,
            "schema_files": schema_files,
            "schema_set_sha256": schema_digest.hexdigest(),
            "sdk": {"name": public_name, "version": "1.2.3"},
        }
        if public_name != "example":
            self.manifest["sdk"]["schema_namespace"] = "example"
        if sdk == "python":
            self.manifest["python_package"] = {
                "build_backend": "setuptools.build_meta",
                "distribution_name": public_name,
                "import_name": public_name,
                "pure_python": True,
                "requires_python": ">=3.10",
                "typing": {
                    "marker": f"src/{public_name}/py.typed",
                    "stubs": [f"src/{public_name}/guest.pyi"],
                },
                "version": "1.2.3",
                "wheel_tag": "py3-none-any",
            }
            self.add_output(f"src/{public_name}/py.typed")
            self.add_output(f"src/{public_name}/guest.pyi")
        if bridge:
            runtimes.append(
                {
                    "language": "python",
                    "package": "flatbuffers",
                    "version_requirement": ">=24.3.25,<26.0.0",
                }
            )
            self.manifest["python_bridge"] = {
                "header": f"python_bridge/{public_name}_python_bridge.h",
                "module_name": f"{public_name}_bridge",
                "registration_function": f"{public_name}::python_bridge::append_inittab",
                "requires_python": ">=3.10",
                "sdk_import_name": public_name,
                "source": f"python_bridge/{public_name}_python_bridge.cpp",
                "wrapper_type": f"{public_name}::python_bridge::scoped_envelope",
            }
            self.add_output(f"python_bridge/{public_name}_python_bridge.h")
            self.add_output(f"python_bridge/{public_name}_python_bridge.cpp")

    @staticmethod
    def create_file(root: Path, relative: str, content: bytes = b"") -> dict[str, object]:
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        return {"path": relative, "sha256": hashlib.sha256(content).hexdigest(), "size": len(content)}

    def add_output(self, relative: str) -> None:
        self.manifest["files"].append(self.create_file(self.sdk_root, relative))
        self.manifest["files"].sort(key=lambda item: item["path"])

    def write(self) -> None:
        self.path.write_text(json.dumps(self.manifest) + "\n", encoding="utf-8")

    def verify(self, with_schema_root: bool = True) -> dict:
        self.write()
        return generator_manifest.verify_generation_manifest(
            self.path, self.schema_root if with_schema_root else None
        )


class GeneratorManifestTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory(prefix="generator-manifest-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.fixture = ManifestFixture(self.root)

    def assert_invalid(self, message: str, with_schema_root: bool = True) -> None:
        with self.assertRaises(ValueError) as raised:
            self.fixture.verify(with_schema_root)
        self.assertEqual(str(raised.exception), message)

    def test_valid_raw_manifests(self) -> None:
        for sdk, bridge in (("cpp", False), ("cpp", True), ("python", False), ("rust", False), ("ts", False)):
            with self.subTest(sdk=sdk, bridge=bridge):
                fixture = ManifestFixture(self.root / f"{sdk}-{bridge}", sdk, bridge)
                self.assertEqual(
                    fixture.manifest["files"][0]["sha256"],
                    # SHA-256 test vector for b"abc".
                    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",  # pragma: allowlist secret
                )
                for with_root in (False, True):
                    self.assertEqual(fixture.verify(with_root), fixture.manifest)

    def test_public_name_can_differ_from_schema_namespace(self) -> None:
        for sdk, bridge in (("python", False), ("cpp", True)):
            with self.subTest(sdk=sdk, bridge=bridge):
                fixture = ManifestFixture(
                    self.root / f"custom-{sdk}",
                    sdk,
                    bridge,
                    public_name="open_perception_kit",
                )
                self.assertEqual(fixture.verify(), fixture.manifest)
                descriptor = "python_package" if sdk == "python" else "python_bridge"
                field = "import_name" if sdk == "python" else "sdk_import_name"
                fixture.manifest[descriptor][field] = "perception"
                with self.assertRaisesRegex(ValueError, f"{descriptor} does not match"):
                    fixture.verify()

    def test_valid_meson_output(self) -> None:
        self.fixture.manifest["outputs"]["integrations"] = ["cmake", "meson"]
        self.fixture.add_output("meson/example/meson.build")
        self.assertEqual(self.fixture.verify(), self.fixture.manifest)

    def test_valid_version_boundaries_and_empty_payloads(self) -> None:
        self.fixture.manifest["sdk"]["version"] = "0.0.0"
        self.fixture.manifest["generator"]["version"] = "0.0.0"
        self.fixture.manifest["payloads"] = []
        for version in ("24.3.25", "25.999.999"):
            with self.subTest(version=version):
                self.fixture.manifest["flatc"].update(
                    semantic_version=version, version=f"flatc version {version}"
                )
                self.fixture.manifest["flatbuffers_runtimes"][0]["version_requirement"] = f"=={version}"
                self.assertEqual(self.fixture.verify(), self.fixture.manifest)

    def test_json_loading_errors(self) -> None:
        self.fixture.path.write_text('{"sdk":{},"sdk":{}}', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "^duplicate JSON key: sdk$"):
            generator_manifest.verify_generation_manifest(self.fixture.path)
        self.fixture.path.write_text('{"sdk":{"name":"a","name":"b"}}', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "^duplicate JSON key: name$"):
            generator_manifest.verify_generation_manifest(self.fixture.path)
        self.fixture.path.write_text("{", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "^invalid JSON in manifest "):
            generator_manifest.verify_generation_manifest(self.fixture.path)
        self.fixture.path.unlink()
        with self.assertRaisesRegex(ValueError, "^cannot read manifest "):
            generator_manifest.verify_generation_manifest(self.fixture.path)

    def test_strict_keys_at_every_validation_section(self) -> None:
        original = copy.deepcopy(self.fixture.manifest)
        for location in ((), ("sdk",), ("generator",), ("flatc",), ("outputs",), ("schema_files", 0), ("payloads", 0), ("files", 0)):
            with self.subTest(location=location):
                self.fixture.manifest = copy.deepcopy(original)
                target = self.fixture.manifest
                label = "manifest"
                for key in location:
                    target = target[key]
                    label = f"{label}[{key}]" if isinstance(key, int) else key
                removed = sorted(target)[0]
                del target[removed]
                target["unknown"] = None
                self.assert_invalid(f"{label} has invalid fields: missing {removed}; unexpected unknown")

    def test_missing_or_unknown_manifest_keys(self) -> None:
        self.fixture.manifest["unknown"] = None
        self.assert_invalid("manifest has invalid fields: unexpected unknown")
        del self.fixture.manifest["unknown"]
        del self.fixture.manifest["files"]
        self.assert_invalid("manifest has invalid fields: missing files")

    def test_container_shapes(self) -> None:
        original = self.fixture.manifest
        self.fixture.manifest = []
        self.assert_invalid("manifest must be a JSON object")
        self.fixture.manifest = original
        for section in ("sdk", "generator", "flatc", "outputs"):
            with self.subTest(section=section):
                previous = self.fixture.manifest[section]
                self.fixture.manifest[section] = []
                self.assert_invalid(f"{section} must be a JSON object")
                self.fixture.manifest[section] = previous
        for section in ("schema_files", "files", "payloads"):
            previous = self.fixture.manifest[section]
            invalid_values = (None, {}) if section == "payloads" else (None, {}, [])
            for value in invalid_values:
                with self.subTest(section=section, value=value):
                    self.fixture.manifest[section] = value
                    kind = "a list" if section == "payloads" else "a non-empty list"
                    self.assert_invalid(f"{section} must be {kind}")
            self.fixture.manifest[section] = [None]
            self.assert_invalid(f"{section}[0] must be a JSON object")
            self.fixture.manifest[section] = previous

    def test_nonempty_strings(self) -> None:
        for section, fields in (("sdk", ("name", "version")), ("generator", ("version",)), ("flatc", ("version", "semantic_version"))):
            for field in fields:
                previous = self.fixture.manifest[section][field]
                for value in (None, 1, ""):
                    with self.subTest(section=section, field=field, value=value):
                        self.fixture.manifest[section][field] = value
                        self.assert_invalid(f"{section}.{field} must be a non-empty string")
                self.fixture.manifest[section][field] = previous

    def test_duplicate_entries(self) -> None:
        for section, message in (
            ("schema_files", "duplicate schema file entry: alpha.fbs"),
            ("files", "duplicate generated file entry: generated.txt"),
        ):
            with self.subTest(section=section):
                entries = self.fixture.manifest[section]
                entries.insert(1, copy.deepcopy(entries[0]))
                self.assert_invalid(message)
                entries.pop(1)
        for field in ("payload_id", "qualified_root_type", "file_identifier", "schema"):
            with self.subTest(field=field):
                payloads = self.fixture.manifest["payloads"]
                previous = payloads[1][field]
                payloads[1][field] = payloads[0][field]
                self.assert_invalid(f"duplicate {field} in payloads: {payloads[0][field]}")
                payloads[1][field] = previous

    def test_unsorted_entries(self) -> None:
        self.fixture.add_output("z.txt")
        for section, key in (("schema_files", "path"), ("payloads", "qualified_root_type"), ("files", "path")):
            with self.subTest(section=section):
                self.fixture.manifest[section].reverse()
                self.assert_invalid(f"{section} must be sorted by {key}")
                self.fixture.manifest[section].reverse()

    def test_schema_root_crosschecks(self) -> None:
        self.fixture.create_file(self.fixture.schema_root, "extra.fbs")
        self.assert_invalid("schema_files do not match the schema root")
        self.assertEqual(self.fixture.verify(False), self.fixture.manifest)
        (self.fixture.schema_root / "extra.fbs").unlink()
        self.fixture.manifest["schema_set_sha256"] = "0" * 64
        self.assert_invalid("schema_set_sha256 does not match the verified schema files")
        self.assertEqual(self.fixture.verify(False), self.fixture.manifest)
        self.fixture.manifest["schema_files"].pop()
        self.assert_invalid("payloads[1].schema is missing from schema_files", False)

    def test_missing_files(self) -> None:
        for section, root in (("schema_files", self.fixture.schema_root), ("files", self.fixture.sdk_root)):
            with self.subTest(section=section):
                path = root / self.fixture.manifest[section][0]["path"]
                content = path.read_bytes()
                path.unlink()
                self.assert_invalid(f"{section}[0] does not exist: {path}")
                path.write_bytes(content)

    def test_extra_or_unlisted_generated_files(self) -> None:
        self.fixture.create_file(self.fixture.sdk_root, "extra.txt")
        self.assert_invalid("files do not match the generated SDK root")
        self.fixture.add_output("extra.txt")
        self.fixture.manifest["files"].pop()
        self.assert_invalid("files do not match the generated SDK root")

    def test_tampered_sizes_and_hashes(self) -> None:
        for section in ("schema_files", "files"):
            with self.subTest(section=section):
                item = self.fixture.manifest[section][0]
                original_size = item["size"]
                for invalid_size in (-1, True, 1.5, "3"):
                    item["size"] = invalid_size
                    self.assert_invalid(f"{section}[0].size must be a non-negative integer")
                item["size"] = original_size + 1
                message = (
                    f"schema_files[0].size mismatch for {item['path']}"
                    if section == "schema_files"
                    else f"files[0].size mismatch: expected {original_size + 1}, found {original_size}"
                )
                self.assert_invalid(message)
                item["size"] = original_size
                actual_digest = item["sha256"]
                item["sha256"] = "0" * 64
                self.assert_invalid(
                    f"{section}[0].sha256 mismatch for {item['path']}: "
                    f"expected {'0' * 64}, found {actual_digest}"
                )
                item["sha256"] = actual_digest.upper()
                self.assert_invalid(f"{section}[0].sha256 must be a lowercase 64-character SHA-256 digest")
                item["sha256"] = actual_digest

    def test_unsafe_paths(self) -> None:
        for section, field in (("schema_files", "path"), ("files", "path"), ("payloads", "schema")):
            item = self.fixture.manifest[section][0]
            original = item[field]
            for path in ("../outside", "/absolute", "C:/absolute", "dir\\file", ".", "dir/../file"):
                with self.subTest(section=section, path=path):
                    item[field] = path
                    self.assert_invalid(f"{section}[0].{field} must be a normalized relative POSIX path")
            item[field] = original

    def test_symlink_escapes_and_nonregular_files(self) -> None:
        outside = self.root / "outside.txt"
        outside.write_bytes(b"abc")
        for section, root in (("schema_files", self.fixture.schema_root), ("files", self.fixture.sdk_root)):
            with self.subTest(section=section):
                path = root / self.fixture.manifest[section][0]["path"]
                content = path.read_bytes()
                path.unlink()
                path.symlink_to(outside)
                self.assert_invalid(f"{section}[0] escapes its logical root: {path}")
                path.unlink()
                path.mkdir()
                self.assert_invalid(f"{section}[0] is not a regular file: {path}")
                path.rmdir()
                path.write_bytes(content)

    def test_manifest_cannot_list_itself(self) -> None:
        self.fixture.manifest["files"][0]["path"] = self.fixture.path.name
        self.assert_invalid("files[0].path must not list the manifest itself")

    def test_identity_validation(self) -> None:
        self.fixture.manifest["sdk"]["name"] = "Example"
        self.assert_invalid("sdk.name must match [a-z][a-z0-9_]*")
        self.fixture.manifest["sdk"]["name"] = "example"
        self.fixture.manifest["generator"]["name"] = "other"
        self.assert_invalid("generator.name must be 'flowdata-sdk'")

    def test_semantic_versions_remain_ascii_and_stable(self) -> None:
        for section, field, suffix in (
            ("sdk", "version", "semantic version"),
            ("generator", "version", "semantic version"),
            ("flatc", "semantic_version", "version"),
        ):
            original = self.fixture.manifest[section][field]
            for value in ("01.2.3", "1.02.3", "1.2.03", "1.2.3-rc1", "1.2.3+build", "1.2", "1.2.3\n", "1\u0661.2.3", "1.2\u0662.3", "1.2.3\u0663"):
                with self.subTest(section=section, value=value):
                    self.fixture.manifest[section][field] = value
                    self.assert_invalid(f"{section}.{field} must be a stable MAJOR.MINOR.PATCH {suffix}")
            self.fixture.manifest[section][field] = original

    def test_compiler_and_runtime_contracts(self) -> None:
        flatc = self.fixture.manifest["flatc"]
        flatc["version"] = "flatc version 25.1.0"
        self.assert_invalid("flatc.semantic_version does not match flatc.version")
        for version in ("24.3.24", "26.0.0"):
            flatc.update(semantic_version=version, version=f"flatc version {version}")
            self.assert_invalid("flatc.semantic_version is outside supported range >=24.3.25,<26.0.0")
        flatc.update(semantic_version="25.9.23", version="flatc version 25.9.23")
        flatc["version_requirement"] = ">=24.0.0"
        self.assert_invalid("flatc.version_requirement must be '>=24.3.25,<26.0.0'")
        flatc["version_requirement"] = ">=24.3.25,<26.0.0"
        self.fixture.manifest["flatbuffers_runtimes"][0]["version_requirement"] = ">=24.3.25,<26.0.0"
        self.assert_invalid("flatbuffers_runtimes does not match the selected SDK, bridge, and compiler")

    def test_output_selection(self) -> None:
        original = copy.deepcopy(self.fixture.manifest["outputs"])
        for field, value, message in (
            ("sdk", "java", "outputs.sdk must be one of: cpp, python, rust, ts"),
            ("cpp_python_bridge", 1, "outputs.cpp_python_bridge must be a boolean"),
            ("integrations", "meson", "outputs.integrations must be a list of strings"),
            ("integrations", [1], "outputs.integrations must be a list of strings"),
            ("integrations", ["meson", "cmake"], "outputs.integrations must be sorted and contain no duplicates"),
            ("integrations", ["meson", "meson"], "outputs.integrations must be sorted and contain no duplicates"),
            ("integrations", ["other"], "outputs.integrations contains unsupported values: other"),
            ("integrations", ["meson"], "outputs.integrations enables meson but files contain no Meson output"),
        ):
            with self.subTest(field=field, value=value):
                self.fixture.manifest["outputs"] = {**original, field: value}
                self.assert_invalid(message)
        for sdk in ("python", "rust", "ts"):
            for feature, value in (("cpp_python_bridge", True), ("integrations", ["cmake"])):
                with self.subTest(sdk=sdk, feature=feature):
                    self.fixture.manifest["outputs"] = {**original, "sdk": sdk, feature: value}
                    self.assert_invalid("C++ integrations and the Python bridge require outputs.sdk 'cpp'")

    def test_payload_metadata(self) -> None:
        original = copy.deepcopy(self.fixture.manifest["payloads"][0])
        for field, value, message in (
            ("file_identifier", "ABC", "file_identifier must contain exactly four characters"),
            ("payload_id", "0x0123456789ABCDEf", "payload_id must match 0x followed by 16 lowercase hex digits"),
            ("payload_id", "0x8000000000000000", "payload_id must use the known-payload id domain"),
            ("schema", "missing.fbs", "schema is missing from schema_files"),
        ):
            with self.subTest(field=field):
                self.fixture.manifest["payloads"][0] = {**original, field: value}
                self.assert_invalid(f"payloads[0].{message}")

    def test_python_descriptors_and_required_files(self) -> None:
        for sdk, bridge, descriptor in (("python", False, "python_package"), ("cpp", True, "python_bridge")):
            with self.subTest(descriptor=descriptor):
                self.fixture = ManifestFixture(self.root / sdk, sdk, bridge)
                expected = copy.deepcopy(self.fixture.manifest[descriptor])
                message = (
                    "python_package does not match the generated Python SDK"
                    if sdk == "python"
                    else "python_bridge does not match the generated C++ bridge"
                )
                for invalid in (None, {}, {**expected, "unknown": None}, {**expected, "requires_python": ">=3.9"}):
                    self.fixture.manifest[descriptor] = invalid
                    self.assert_invalid(message)
                del self.fixture.manifest[descriptor]
                self.assert_invalid(message)
                self.fixture.manifest[descriptor] = expected
                removed = self.fixture.manifest["files"].pop()
                self.assert_invalid(f"descriptor files are missing from files: {removed['path']}")

    def test_descriptors_for_unselected_features(self) -> None:
        for descriptor, message in (
            ("python_package", "python_package does not match the generated Python SDK"),
            ("python_bridge", "python_bridge does not match the generated C++ bridge"),
        ):
            with self.subTest(descriptor=descriptor):
                self.fixture.manifest[descriptor] = {}
                self.assert_invalid(message)
                self.fixture.manifest[descriptor] = None
                self.assertEqual(self.fixture.verify(), self.fixture.manifest)

    def test_validation_order(self) -> None:
        self.fixture.manifest["flatc"]["semantic_version"] = "invalid"
        self.fixture.manifest["schema_files"][0]["size"] = -1
        self.assert_invalid("schema_files[0].size must be a non-negative integer")
        self.fixture.manifest["schema_files"][0]["path"] = "../outside"
        self.assert_invalid("schema_files[0].path must be a normalized relative POSIX path")
        self.fixture = ManifestFixture(self.root / "order")
        self.fixture.manifest["files"][0].update(path="missing.txt", size=-1)
        self.assert_invalid(f"files[0] does not exist: {self.fixture.sdk_root / 'missing.txt'}")


class PythonPackageNamingTests(unittest.TestCase):
    def test_public_name_is_used_for_wire_sdk_identity(self) -> None:
        source = python_codegen._sdk_text(
            "open_perception_kit",
            "open_perception_kit",
            "1.2.3",
            "0" * 64,
        )

        self.assertIn('SDK_NAME = "open_perception_kit"', source)
        self.assertIn(
            '"open_perception_kit.internalfb.WireEnvelope"', source
        )
        self.assertIn(
            '"open_perception_kit.internalfb.WirePayload"', source
        )
        self.assertNotIn('"perception.internalfb.', source)

    def test_guest_uses_public_bridge_module(self) -> None:
        source = python_codegen._guest_text("open_perception_kit")

        self.assertIn("from open_perception_kit_bridge import Envelope", source)
        self.assertIn("open_perception_kit.guest is available", source)


if __name__ == "__main__":
    unittest.main()
