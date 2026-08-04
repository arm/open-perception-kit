################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import hashlib
import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from dataclasses import replace


PACKAGE_MODULE_PATH = Path(__file__).resolve().parents[1] / "package.py"
sys.path.insert(0, str(PACKAGE_MODULE_PATH.parent))
release_artifacts = importlib.import_module("artifacts")

SPEC = importlib.util.spec_from_file_location("perception_release_package", PACKAGE_MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
release_package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release_package)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class CommandHelpTests(unittest.TestCase):
    def run_help(self, *arguments: str) -> str:
        result = subprocess.run(
            [sys.executable, str(PACKAGE_MODULE_PATH.parent / "cli.py"), *arguments, "--help"],
            check=True,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.stderr, "")
        return result.stdout

    def test_top_level_help_documents_commands_and_examples(self) -> None:
        output = self.run_help()
        self.assertIn("./scripts/perception-sdk.sh <command> --help", output)
        self.assertIn("package", output)
        self.assertIn("install-dev", output)

    def test_generate_and_check_help_document_tool_overrides(self) -> None:
        for command in ("generate", "check"):
            with self.subTest(command=command):
                output = self.run_help(command)
                self.assertIn(f"./scripts/perception-sdk.sh {command}", output)
                self.assertIn("FlatBuffers compiler", output)
                self.assertIn("autopep8", output)

    def test_package_help_documents_release_controls(self) -> None:
        output = self.run_help("package")
        self.assertIn("never regenerates SDK files", output)
        self.assertIn("does not override the descriptor", output)
        self.assertIn("dirty=true", output)
        self.assertIn("provenance", output)
        self.assertIn("read-write cache", output)

    def test_verify_help_documents_sidecar_behavior(self) -> None:
        output = self.run_help("verify")
        self.assertIn("Existing sidecars are always checked", output)
        self.assertIn("require and verify both", output)

    def test_install_dev_help_documents_target_and_cache(self) -> None:
        output = self.run_help("install-dev")
        self.assertIn("target Python interpreter", output)
        self.assertIn("checksum-locked FlatBuffers", output)
        self.assertIn("wheel", output)


class SemanticVersionTests(unittest.TestCase):
    def test_accepts_stable_semantic_version(self) -> None:
        release_package.require_semantic_version("1.2.3")

    def test_rejects_non_release_versions(self) -> None:
        for version in ("1.2", "v1.2.3", "1.2.3-rc1", "1.2.3+build"):
            with self.subTest(version=version), self.assertRaises(RuntimeError):
                release_package.require_semantic_version(version)


class SdkDescriptorTests(unittest.TestCase):
    def test_descriptor_is_the_authoritative_release_configuration(self) -> None:
        config = release_package.perception_config.load_sdk_config()
        descriptor = json.loads(config.descriptor_path.read_text(encoding="utf-8"))
        self.assertEqual(config.name, descriptor["name"])
        self.assertEqual(config.version, descriptor["version"])
        self.assertIsNotNone(release_package.SEMANTIC_VERSION_RE.fullmatch(config.version))
        self.assertEqual(config.flatbuffers_version, descriptor["flatbuffers"]["version"])
        self.assertEqual(
            config.flatbuffers_wheel.sha256,
            descriptor["flatbuffers"]["python_wheel"]["sha256"],
        )
        self.assertEqual(
            config.flatbuffers_source.sha256,
            descriptor["flatbuffers"]["source_archive"]["sha256"],
        )
        self.assertEqual(
            [tool.name for tool in config.python_build_tools],
            ["pip", "setuptools", "wheel"],
        )
        for artifact in (
            config.flatbuffers_wheel,
            config.flatbuffers_source,
            *config.python_build_tools,
        ):
            self.assertTrue(artifact.url.startswith("https://"))
            self.assertTrue(artifact.url.endswith(artifact.filename))
            self.assertRegex(artifact.sha256, r"^[0-9a-f]{64}$")
        self.assertEqual(
            config.flowdata_generator.relative_to(config.flowdata_root).as_posix(),
            descriptor["flowdata_sdk"]["generator"],
        )
        self.assertEqual(
            config.internal_meson_path.relative_to(config.generated_root.parents[1]).as_posix(),
            descriptor["project_generated_files"]["internal_meson"],
        )

    def test_descriptor_rejects_unknown_fields(self) -> None:
        descriptor = json.loads(
            release_package.perception_config.SDK_CONFIG_PATH.read_text(encoding="utf-8")
        )
        descriptor["duplicate_version"] = "9.9.9"
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sdk.json"
            path.write_text(json.dumps(descriptor), encoding="utf-8")
            with self.assertRaises(RuntimeError):
                release_package.perception_config.load_sdk_config(path)


class ArtifactCacheTests(unittest.TestCase):
    def test_downloads_once_and_reuses_verified_cache(self) -> None:
        content = b"locked artifact\n"
        artifact = release_package.perception_config.LockedArtifact(
            name="example",
            version="1.2.3",
            filename="example.whl",
            url="https://example.invalid/example.whl",
            sha256=hashlib.sha256(content).hexdigest(),
        )
        calls: list[str] = []

        def download(url: str, destination: Path) -> None:
            calls.append(url)
            Path(destination).write_bytes(content)

        original = release_artifacts.urllib.request.urlretrieve
        release_artifacts.urllib.request.urlretrieve = download
        try:
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                first = release_artifacts.acquire_artifact(
                    artifact, root / "first", cache_dir=root / "cache"
                )
                second = release_artifacts.acquire_artifact(
                    artifact, root / "second", cache_dir=root / "cache"
                )
                self.assertEqual(first.read_bytes(), content)
                self.assertEqual(second.read_bytes(), content)
                self.assertEqual(calls, [artifact.url])
        finally:
            release_artifacts.urllib.request.urlretrieve = original


class GenerationReceiptTests(unittest.TestCase):
    def test_rejects_schema_changes_without_regeneration(self) -> None:
        config = release_package.perception_config.load_sdk_config()
        with tempfile.TemporaryDirectory() as tmp:
            schema_dir = Path(tmp) / "metadata"
            shutil.copytree(config.schema_dir, schema_dir)
            schema = next(schema_dir.rglob("*.fbs"))
            schema.write_bytes(schema.read_bytes() + b"\n")
            with self.assertRaisesRegex(RuntimeError, "schema inputs are stale"):
                release_package.perception_generate.verify_perception_manifest(
                    replace(config, schema_dir=schema_dir)
                )


class SingleSourceContractTests(unittest.TestCase):
    def test_consumers_do_not_redeclare_sdk_configuration(self) -> None:
        repository = Path(__file__).resolve().parents[3]
        dockerfile = (repository / "Dockerfile").read_text(encoding="utf-8")
        self.assertNotIn("ARG FLATBUFFERS_VERSION", dockerfile)
        self.assertIn("install-perception-flatbuffers", dockerfile)
        self.assertFalse((repository / "Dockerfile.dev").exists())

        plumber = (repository / "tools" / "plumber" / "pyproject.toml").read_text(
            encoding="utf-8"
        )
        self.assertNotIn('"flatbuffers==', plumber)

        devsetup = (repository / ".devcontainer" / "devsetup.sh").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("generated/perception", devsetup)
        self.assertIn("scripts/perception-sdk.sh", devsetup)
        self.assertIn("install-dev", devsetup)

        self.assertFalse((repository / "scripts" / "gen-perception.sh").exists())
        self.assertFalse((repository / "scripts" / "package-perception-sdk.sh").exists())
        self.assertTrue((repository / "scripts" / "perception-sdk.sh").is_file())

        packager = (repository / "tools" / "perception" / "package.py").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("check_generated", packager)
        self.assertNotIn('add_argument("--flatc"', packager)
        self.assertNotIn('add_argument("--clang-format"', packager)


class BundleVerificationTests(unittest.TestCase):
    def create_wheel(
        self,
        path: Path,
        name: str,
        version: str,
        requirements: list[str] | None = None,
    ) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        distribution = name.replace("-", "_")
        metadata = [
            "Metadata-Version: 2.1",
            f"Name: {name}",
            f"Version: {version}",
        ]
        metadata.extend(f"Requires-Dist: {requirement}" for requirement in requirements or [])
        with zipfile.ZipFile(path, "w") as wheel:
            wheel.writestr(
                f"{distribution}-{version}.dist-info/METADATA",
                "\n".join(metadata) + "\n",
            )
            wheel.writestr(
                f"{distribution}-{version}.dist-info/WHEEL",
                "Wheel-Version: 1.0\nTag: py3-none-any\n",
            )

    def create_bundle(self, root: Path) -> Path:
        bundle = root / "perception-sdk-1.2.3"
        files = {
            "cpp/perception.h": b"header\n",
            "metadata/perception-sdk-manifest.json": b"{}\n",
            "metadata/sdk.json": b"{}\n",
            "schemas/payload.fbs": b"namespace perception.metadata;\n",
        }
        for relative_path, content in files.items():
            path = bundle / relative_path
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        self.create_wheel(bundle / "python/flatbuffers.whl", "flatbuffers", "25.9.23")
        self.create_wheel(
            bundle / "python/perception.whl",
            "perception",
            "1.2.3",
            ["flatbuffers>=24.3.25,<26.0.0"],
        )
        files.update({
            "python/flatbuffers.whl": b"",
            "python/perception.whl": b"",
        })
        schema_root = bundle / "schemas"
        schema_files = release_package.perception_generate._schema_records(schema_root)
        schema_digest = release_package.perception_generate._schema_set_sha256(schema_root)
        (bundle / "metadata/perception-sdk-manifest.json").write_text(
            json.dumps({
                "upstream_receipts": {
                    "cpp": {
                        "schema_files": schema_files,
                        "schema_set_sha256": schema_digest,
                    }
                }
            }),
            encoding="utf-8",
        )
        manifest = {
            "archive": {
                "compression": "stored",
                "file_mode": "0644",
                "timestamp": "1980-01-01T00:00:00Z",
                "top_level_directory": bundle.name,
            },
            "artifact": {"name": "perception-sdk", "version": "1.2.3"},
            "generator": {
                "commit": "0" * 40,
                "name": "flowdata-sdk",
                "version": "0.2.0",
            },
            "files": [
                {
                    "path": relative_path,
                    "sha256": digest(bundle / relative_path),
                    "size": (bundle / relative_path).stat().st_size,
                }
                for relative_path in sorted(files)
            ],
            "flatbuffers": {
                "python_wheel": {
                    "filename": "flatbuffers.whl",
                    "name": "flatbuffers",
                    "path": "python/flatbuffers.whl",
                    "sha256": digest(bundle / "python/flatbuffers.whl"),
                    "url": "https://example.invalid/flatbuffers.whl",
                    "version": "25.9.23",
                }
            },
            "outputs": {
                "cpp": {
                    "cpp_python_bridge": True,
                    "integrations": ["cmake", "meson"],
                    "sdk": "cpp",
                },
                "python": {"sdk": "python"},
                "python_bridge": {},
                "python_package": {},
                "schemas": True,
            },
            "payloads": [],
            "perception_wheel": {
                "path": "python/perception.whl",
                "sha256": digest(bundle / "python/perception.whl"),
            },
            "postprocessing": {},
            "schemas": {
                "files": schema_files,
                "root": "schemas",
                "sha256": schema_digest,
            },
            "schema_set_sha256": schema_digest,
            "source": {
                "descriptor": {
                    "path": "metadata/sdk.json",
                    "sha256": digest(bundle / "metadata/sdk.json"),
                },
                "dirty": False,
                "generated_manifest": {
                    "path": "metadata/perception-sdk-manifest.json",
                    "sha256": digest(bundle / "metadata/perception-sdk-manifest.json"),
                },
                "input_tree_sha256": "0" * 64,
                "release_tools": [{"path": "tools/package.py", "sha256": "0" * 64}],
            },
            "tools": {},
        }
        (bundle / release_package.MANIFEST_FILENAME).write_text(
            json.dumps(manifest), encoding="utf-8"
        )
        return bundle

    def test_verifies_manifest_and_archive(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bundle = self.create_bundle(root)
            release_package.verify_bundle(bundle)
            first = root / "first.zip"
            second = root / "second.zip"
            release_package.write_deterministic_zip(bundle, first)
            release_package.write_deterministic_zip(bundle, second)
            release_package.verify_zip(bundle, first)
            release_package.verify_release_archive(first)
            release_package.write_checksum(first)
            first.with_suffix(first.suffix + ".provenance.json").write_text(
                json.dumps({
                    "archive": {"path": first.name, "sha256": digest(first)},
                    "descriptor_sha256": digest(bundle / "metadata/sdk.json"),
                    "dirty": False,
                    "generated_manifest_sha256": digest(
                        bundle / "metadata/perception-sdk-manifest.json"
                    ),
                    "repository_commit": "0" * 40,
                }),
                encoding="utf-8",
            )
            release_package.verify_release_sidecars(first)
            self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_rejects_modified_content(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            bundle = self.create_bundle(Path(tmp))
            (bundle / "cpp" / "perception.h").write_text("changed\n", encoding="utf-8")
            with self.assertRaises(RuntimeError):
                release_package.verify_bundle(bundle)

    def test_rejects_unsafe_manifest_path(self) -> None:
        with self.assertRaises(RuntimeError):
            release_package.validate_relative_path("../outside")
        with self.assertRaises(RuntimeError):
            release_package.validate_relative_path("C:\\outside")

    def test_rejects_manifest_version_field(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            bundle = self.create_bundle(Path(tmp))
            manifest_path = bundle / release_package.MANIFEST_FILENAME
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["manifest_version"] = 1
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "fields are stale"):
                release_package.verify_bundle(bundle)


class GeneratedSdkTests(unittest.TestCase):
    def test_checked_in_sdk_has_release_integrations_and_typing(self) -> None:
        repository = Path(__file__).resolve().parents[3]
        config = release_package.perception_config.load_sdk_config()
        generated = config.generated_root
        self.assertTrue((generated / "cpp" / "cmake" / "perception.cmake").is_file())
        self.assertTrue((generated / "cpp" / "meson" / "perception" / "meson.build").is_file())
        self.assertTrue((generated / "python" / "src" / "perception" / "guest.pyi").is_file())
        self.assertTrue((generated / "python" / "src" / "perception" / "py.typed").is_file())

        manifest = json.loads(
            (generated / "perception-sdk-manifest.json").read_text(encoding="utf-8")
        )
        self.assertNotIn("manifest_version", manifest)
        self.assertEqual(
            manifest["descriptor"]["path"],
            config.descriptor_path.relative_to(repository).as_posix(),
        )
        self.assertEqual(
            manifest["project_files"][0]["path"],
            config.internal_meson_path.relative_to(repository).as_posix(),
        )
        self.assertNotIn("manifest_version", manifest["upstream_receipts"]["cpp"])
        self.assertNotIn("manifest_version", manifest["upstream_receipts"]["python"])
        self.assertTrue(
            (generated / "cpp" / "meson" / "perception" / "python_bridge" / "meson.build").is_file()
        )
        self.assertEqual(
            manifest["upstream_receipts"]["python"]["python_package"]["typing"],
            {
                "marker": "src/perception/py.typed",
                "stubs": ["src/perception/guest.pyi"],
            },
        )


if __name__ == "__main__":
    unittest.main()
