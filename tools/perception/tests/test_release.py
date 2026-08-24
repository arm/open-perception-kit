################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import hashlib
import importlib.util
import json
import shutil
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from dataclasses import replace


PACKAGE_MODULE_PATH = Path(__file__).resolve().parents[1] / "package.py"
sys.path.insert(0, str(PACKAGE_MODULE_PATH.parent))
release_artifacts = importlib.import_module("artifacts")
schema_change = importlib.import_module("evaluate_schema_change")

SPEC = importlib.util.spec_from_file_location("perception_release_package", PACKAGE_MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
release_package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release_package)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class SchemaChangeParserTests(unittest.TestCase):
    def test_field_parser_handles_types_and_defaults(self) -> None:
        fields = schema_change.parse_fields(
            """
            label: string;
            values: [float];
            threshold: float = 0.5;
            """
        )

        self.assertEqual(
            fields,
            (
                schema_change.Field("label", "string", None),
                schema_change.Field("values", "[float]", None),
                schema_change.Field("threshold", "float", "0.5"),
            ),
        )

    def test_field_parser_ignores_malformed_declarations(self) -> None:
        self.assertEqual(
            schema_change.parse_fields(
                "missing_type: ;\nmissing_colon;\nvalue:int = ;\nextra:int;;"
            ),
            (),
        )

    def test_schema_parser_preserves_layout_and_wire_attributes(self) -> None:
        schema = schema_change.parse_schema(
            "metadata.fbs",
            """
            struct Point {
                x:float;
                y:float;
            }
            enum State : byte { UNKNOWN = 0, READY = 1 }
            table Payload {
                point:Point (required, id: 0);
            }
            """,
        )

        self.assertEqual(
            schema.structs["Point"],
            (
                schema_change.Field("x", "float", None),
                schema_change.Field("y", "float", None),
            ),
        )
        self.assertEqual(
            schema.tables["Payload"][0].attributes,
            ("id: 0", "required"),
        )
        self.assertEqual(schema.sequences["enum:State"].underlying_type, "byte")

    def test_schema_comparison_rejects_wire_breaks(self) -> None:
        old = schema_change.parse_schema(
            "metadata.fbs",
            """
            struct Point {
                x:float;
                y:float;
            }
            enum State : byte { UNKNOWN = 0 }
            table Payload {
                label:string;
            }
            """,
        )
        new = schema_change.parse_schema(
            "metadata.fbs",
            """
            struct Point {
                x:float;
                y:float;
                z:float;
            }
            enum State : int { UNKNOWN = 0 }
            table Payload {
                label:string;
                value:string (required);
            }
            """,
        )

        findings = schema_change.compare_schema(old, new)
        messages = {finding.message: finding.severity for finding in findings}
        self.assertEqual(messages["Point appended fields: z"], "breaking")
        self.assertEqual(messages["changed underlying type of enum:State"], "breaking")
        self.assertEqual(messages["Payload appended fields: value"], "breaking")


class SemanticVersionTests(unittest.TestCase):
    def test_accepts_stable_semantic_version(self) -> None:
        release_package.require_semantic_version("1.2.3")

    def test_rejects_non_release_versions(self) -> None:
        for version in ("1.2", "v1.2.3", "1.2.3-rc1", "1.2.3+build"):
            with self.subTest(version=version), self.assertRaises(RuntimeError):
                release_package.require_semantic_version(version)

    def test_detached_source_commits_require_a_valid_pair(self) -> None:
        self.assertEqual(
            release_package.detached_source_commits("a" * 40, "b" * 40),
            ("a" * 40, "b" * 40),
        )
        with self.assertRaisesRegex(RuntimeError, "must be supplied together"):
            release_package.detached_source_commits("a" * 40, None)


class SdkDescriptorTests(unittest.TestCase):
    def test_descriptor_and_product_version_are_the_release_configuration(self) -> None:
        config = release_package.perception_config.load_sdk_config()
        descriptor = json.loads(config.descriptor_path.read_text(encoding="utf-8"))
        self.assertEqual(config.name, descriptor["name"])
        self.assertNotIn("version", descriptor)
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
        self.assertEqual(config.typescript_runtime.name, "flatbuffers")
        self.assertEqual(config.typescript_runtime.version, config.flatbuffers_version)
        self.assertEqual(config.typescript_compiler.name, "typescript")
        self.assertGreaterEqual(config.node_minimum_major, 20)
        for artifact in (
            config.flatbuffers_wheel,
            config.flatbuffers_source,
            *config.python_build_tools,
            config.typescript_runtime,
            config.typescript_compiler,
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

    def test_product_version_is_read_from_meson(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            meson = Path(tmp) / "meson.build"
            meson.write_text("project('demo', version: '1.2.3')\n", encoding="utf-8")
            self.assertEqual(
                release_package.perception_config.product_version(meson), "1.2.3"
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
    def test_validates_detached_flowdata_identity(self) -> None:
        config = release_package.perception_config.load_sdk_config()
        manifest = json.loads(
            (config.generated_root / "perception-sdk-manifest.json").read_text(
                encoding="utf-8"
            )
        )
        commit = manifest["generation"]["flowdata_sdk"]["commit"]
        release_package.verify_detached_manifest(config, commit)
        with self.assertRaisesRegex(RuntimeError, "flowdata-sdk changed"):
            release_package.verify_detached_manifest(config, "0" * 40)

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


class BundleVerificationTests(unittest.TestCase):
    def create_npm_package(
        self,
        root: Path,
        destination: Path,
        name: str,
        version: str,
        dependencies: dict[str, str] | None = None,
    ) -> None:
        source = root / f"{name}-npm-source"
        (source / "dist" / name).mkdir(parents=True)
        (source / "src" / name).mkdir(parents=True)
        (source / "dist" / name / "index.js").write_text("export {};\n", encoding="utf-8")
        (source / "src" / name / "index.ts").write_text("export {};\n", encoding="utf-8")
        (source / "package.json").write_text(
            json.dumps({
                "name": name,
                "version": version,
                "dependencies": dependencies or {},
            }),
            encoding="utf-8",
        )
        release_package.write_deterministic_npm_package(source, destination)

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
        flatbuffers_wheel_path = bundle / "python/flatbuffers.whl"
        flatbuffers_typescript_path = bundle / "typescript/flatbuffers-25.9.23.tgz"
        files = {
            "cpp/perception.h": b"header\n",
            "metadata/perception-sdk-manifest.json": b"{}\n",
            "schemas/payload.fbs": b"namespace perception.metadata;\n",
        }
        for relative_path, content in files.items():
            path = bundle / relative_path
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        self.create_wheel(flatbuffers_wheel_path, "flatbuffers", "25.9.23")
        self.create_wheel(
            bundle / "python/opk_perception_sdk.whl",
            "opk-perception-sdk",
            "1.2.3",
            ["flatbuffers>=24.3.25,<26.0.0"],
        )
        self.create_npm_package(
            root,
            flatbuffers_typescript_path,
            "flatbuffers",
            "25.9.23",
        )
        self.create_npm_package(
            root,
            bundle / "typescript/perception-1.2.3.tgz",
            "perception",
            "1.2.3",
            {"flatbuffers": "25.9.23"},
        )
        (bundle / "metadata/sdk.json").write_text(
            json.dumps({
                "flatbuffers": {
                    "python_wheel": {
                        "filename": flatbuffers_wheel_path.name,
                        "sha256": digest(flatbuffers_wheel_path),
                        "url": f"https://example.invalid/{flatbuffers_wheel_path.name}",
                    },
                    "source_archive": {
                        "filename": "v25.9.23.tar.gz",
                        "sha256": "1" * 64,
                        "url": "https://example.invalid/v25.9.23.tar.gz",
                    },
                    "version": "25.9.23",
                },
                "name": "perception",
                "typescript_build": {
                    "flatbuffers_runtime": {
                        "filename": flatbuffers_typescript_path.name,
                        "name": "flatbuffers",
                        "sha256": digest(flatbuffers_typescript_path),
                        "url": f"https://example.invalid/{flatbuffers_typescript_path.name}",
                        "version": "25.9.23",
                    }
                },
            }),
            encoding="utf-8",
        )
        files["metadata/sdk.json"] = b""
        files.update({
            "python/flatbuffers.whl": b"",
            "python/opk_perception_sdk.whl": b"",
            "typescript/flatbuffers-25.9.23.tgz": b"",
            "typescript/perception-1.2.3.tgz": b"",
        })
        schema_root = bundle / "schemas"
        schema_files = release_package.perception_generate._schema_records(schema_root)
        schema_digest = release_package.perception_generate._schema_set_sha256(schema_root)
        descriptor_sha256 = digest(bundle / "metadata/sdk.json")
        (bundle / "metadata/perception-sdk-manifest.json").write_text(
            json.dumps({
                "artifact": {"name": "perception-sdk", "version": "1.2.3"},
                "descriptor": {
                    "path": "tools/perception/sdk.json",
                    "sha256": descriptor_sha256,
                },
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
                "compiler": {
                    "semantic_version": "25.9.23",
                    "version": "flatc version 25.9.23",
                },
                "python_wheel": {
                    "filename": "flatbuffers.whl",
                    "name": "flatbuffers",
                    "path": "python/flatbuffers.whl",
                    "sha256": digest(bundle / "python/flatbuffers.whl"),
                    "url": "https://example.invalid/flatbuffers.whl",
                    "version": "25.9.23",
                },
                "source_archive": {
                    "filename": "v25.9.23.tar.gz",
                    "name": "flatbuffers",
                    "sha256": "1" * 64,
                    "url": "https://example.invalid/v25.9.23.tar.gz",
                    "version": "25.9.23",
                },
                "typescript_package": {
                    "filename": "flatbuffers-25.9.23.tgz",
                    "name": "flatbuffers",
                    "path": "typescript/flatbuffers-25.9.23.tgz",
                    "sha256": digest(bundle / "typescript/flatbuffers-25.9.23.tgz"),
                    "url": "https://example.invalid/flatbuffers-25.9.23.tgz",
                    "version": "25.9.23",
                },
            },
            "outputs": {
                "cpp": {
                    "cpp_python_bridge": True,
                    "integrations": ["cmake", "meson"],
                    "sdk": "cpp",
                },
                "python": {"sdk": "python"},
                "typescript": {"sdk": "ts"},
                "python_bridge": {},
                "python_package": {},
                "schemas": True,
            },
            "payloads": [],
            "perception_wheel": {
                "path": "python/opk_perception_sdk.whl",
                "sha256": digest(bundle / "python/opk_perception_sdk.whl"),
            },
            "perception_npm_package": {
                "path": "typescript/perception-1.2.3.tgz",
                "sha256": digest(bundle / "typescript/perception-1.2.3.tgz"),
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
                    "sha256": descriptor_sha256,
                },
                "dirty": False,
                "generated_manifest": {
                    "path": "metadata/perception-sdk-manifest.json",
                    "sha256": digest(bundle / "metadata/perception-sdk-manifest.json"),
                },
                "input_tree_sha256": release_package.content_digest({
                    "descriptor": descriptor_sha256,
                    "generated_manifest": digest(
                        bundle / "metadata/perception-sdk-manifest.json"
                    ),
                    "schema_set": schema_digest,
                }),
                "release_tools": [{"path": "tools/package.py", "sha256": "0" * 64}],
            },
            "tools": {},
        }
        (bundle / release_package.MANIFEST_FILENAME).write_text(
            json.dumps(manifest), encoding="utf-8"
        )
        return bundle

    def rewrite_descriptor_identity(
        self, bundle: Path, descriptor: dict[str, object]
    ) -> None:
        descriptor_path = bundle / "metadata/sdk.json"
        descriptor_path.write_text(json.dumps(descriptor), encoding="utf-8")
        descriptor_sha256 = digest(descriptor_path)

        generated_path = bundle / "metadata/perception-sdk-manifest.json"
        generated = json.loads(generated_path.read_text(encoding="utf-8"))
        generated["descriptor"]["sha256"] = descriptor_sha256
        generated_path.write_text(json.dumps(generated), encoding="utf-8")
        generated_sha256 = digest(generated_path)

        manifest_path = bundle / release_package.MANIFEST_FILENAME
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["source"]["descriptor"]["sha256"] = descriptor_sha256
        manifest["source"]["generated_manifest"]["sha256"] = generated_sha256
        manifest["source"]["input_tree_sha256"] = release_package.content_digest({
            "descriptor": descriptor_sha256,
            "generated_manifest": generated_sha256,
            "schema_set": manifest["schema_set_sha256"],
        })
        for relative, digest_value in (
            ("metadata/sdk.json", descriptor_sha256),
            ("metadata/perception-sdk-manifest.json", generated_sha256),
        ):
            record = next(
                entry for entry in manifest["files"] if entry["path"] == relative
            )
            record["sha256"] = digest_value
            record["size"] = (bundle / relative).stat().st_size
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

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

    def test_npm_package_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            first = root / "first.tgz"
            second = root / "second.tgz"
            self.create_npm_package(root, first, "perception", "1.2.3")
            shutil.rmtree(root / "perception-npm-source")
            self.create_npm_package(root, second, "perception", "1.2.3")
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(
                release_package.npm_package_metadata(first)["name"], "perception"
            )

    def test_rejects_oversized_npm_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "source"
            source.mkdir()
            (source / "package.json").write_bytes(
                b"x" * (release_package.MAX_NPM_METADATA_BYTES + 1)
            )
            package = root / "oversized.tgz"
            release_package.write_deterministic_npm_package(source, package)

            with self.assertRaisesRegex(RuntimeError, "metadata is too large"):
                release_package.npm_package_metadata(package)

    def test_rejects_modified_content(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            bundle = self.create_bundle(Path(tmp))
            (bundle / "cpp" / "perception.h").write_text("changed\n", encoding="utf-8")
            with self.assertRaises(RuntimeError):
                release_package.verify_bundle(bundle)

    def test_rejects_self_declared_input_tree_digest(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            bundle = self.create_bundle(Path(tmp))
            manifest_path = bundle / release_package.MANIFEST_FILENAME
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["source"]["input_tree_sha256"] = "1" * 64
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "input tree identity"):
                release_package.verify_bundle(bundle)

    def test_rejects_descriptor_artifact_identity_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            bundle = self.create_bundle(Path(tmp))
            descriptor_path = bundle / "metadata/sdk.json"
            descriptor = json.loads(descriptor_path.read_text(encoding="utf-8"))
            descriptor["name"] = "other_sdk"
            descriptor_path.write_text(json.dumps(descriptor), encoding="utf-8")

            manifest_path = bundle / release_package.MANIFEST_FILENAME
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            descriptor_sha256 = digest(descriptor_path)
            manifest["source"]["descriptor"]["sha256"] = descriptor_sha256
            manifest["source"]["input_tree_sha256"] = release_package.content_digest({
                "descriptor": descriptor_sha256,
                "generated_manifest": manifest["source"]["generated_manifest"]["sha256"],
                "schema_set": manifest["schema_set_sha256"],
            })
            descriptor_record = next(
                record for record in manifest["files"]
                if record["path"] == "metadata/sdk.json"
            )
            descriptor_record["sha256"] = descriptor_sha256
            descriptor_record["size"] = descriptor_path.stat().st_size
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            with self.assertRaisesRegex(RuntimeError, "artifact identities differ"):
                release_package.verify_bundle(bundle)

    def test_rejects_descriptor_flatbuffers_version_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            bundle = self.create_bundle(Path(tmp))
            descriptor_path = bundle / "metadata/sdk.json"
            descriptor = json.loads(descriptor_path.read_text(encoding="utf-8"))
            descriptor["flatbuffers"]["version"] = "9.9.9"
            descriptor["typescript_build"]["flatbuffers_runtime"]["version"] = "9.9.9"
            self.rewrite_descriptor_identity(bundle, descriptor)

            with self.assertRaisesRegex(RuntimeError, "version does not match compiler"):
                release_package.verify_bundle(bundle)

    def test_rejects_descriptor_flatbuffers_package_lock_mismatch(self) -> None:
        mutations = (
            ("python_wheel", "flatbuffers", "python_wheel", "filename", "other.whl"),
            ("python_wheel", "flatbuffers", "python_wheel", "sha256", "2" * 64),
            (
                "source_archive",
                "flatbuffers",
                "source_archive",
                "filename",
                "other.tar.gz",
            ),
            ("source_archive", "flatbuffers", "source_archive", "sha256", "2" * 64),
            (
                "typescript_package",
                "typescript_build",
                "flatbuffers_runtime",
                "filename",
                "other.tgz",
            ),
            (
                "typescript_package",
                "typescript_build",
                "flatbuffers_runtime",
                "sha256",
                "2" * 64,
            ),
        )
        for manifest_key, section_key, descriptor_key, field, value in mutations:
            with self.subTest(manifest_key=manifest_key, field=field):
                with tempfile.TemporaryDirectory() as tmp:
                    bundle = self.create_bundle(Path(tmp))
                    descriptor_path = bundle / "metadata/sdk.json"
                    descriptor = json.loads(
                        descriptor_path.read_text(encoding="utf-8")
                    )
                    descriptor_lock = descriptor[section_key][descriptor_key]
                    descriptor_lock[field] = value
                    if field == "filename":
                        descriptor_lock["url"] = f"https://example.invalid/{value}"
                    self.rewrite_descriptor_identity(bundle, descriptor)

                    with self.assertRaisesRegex(
                        RuntimeError, f"{manifest_key} lock does not match manifest"
                    ):
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
        self.assertTrue((generated / "ts" / "src" / "perception" / "index.ts").is_file())
        self.assertTrue((generated / "ts" / "dist" / "perception" / "index.js").is_file())
        self.assertTrue((generated / "ts" / "dist" / "perception" / "index.d.ts").is_file())

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
        self.assertNotIn("manifest_version", manifest["upstream_receipts"]["ts"])
        self.assertEqual(manifest["upstream_receipts"]["ts"]["outputs"]["sdk"], "ts")
        self.assertEqual(
            manifest["postprocessing"]["typescript"]["flatbuffers_runtime"],
            config.typescript_runtime.version,
        )
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
