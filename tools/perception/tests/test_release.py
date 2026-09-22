################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import hashlib
import importlib.util
import io
import json
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from contextlib import redirect_stderr, redirect_stdout
from dataclasses import replace
from pathlib import Path
from unittest.mock import patch


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


def cargo_is_usable() -> bool:
    cargo = shutil.which("cargo")
    if cargo is None:
        return False
    return subprocess.run(
        [cargo, "--version"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    ).returncode == 0


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

    def test_report_outputs_json_and_human_text(self) -> None:
        report = {
            "base": "HEAD",
            "base_version": "1.2.3",
            "current_version": "1.2.3",
            "required_bump": "none",
            "changed_schemas": [],
            "affected_roots": [],
            "findings": [],
        }
        output = io.StringIO()
        with (
            patch.object(sys, "argv", ["evaluate_schema_change.py", "--json"]),
            patch.object(schema_change, "repository_root", return_value=Path(".")),
            patch.object(schema_change, "evaluate", return_value=report),
            redirect_stdout(output),
        ):
            self.assertEqual(schema_change.main(), 0)
        self.assertEqual(json.loads(output.getvalue()), report)

        output = io.StringIO()
        with redirect_stdout(output):
            schema_change.print_report(report)
        self.assertIn("OPK version: 1.2.3 -> 1.2.3", output.getvalue())
        self.assertIn("Required OPK release impact: none", output.getvalue())

    def test_rejects_invalid_product_version(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "stable MAJOR.MINOR.PATCH"):
            schema_change.parse_product_version("project('opk', version: 'next')")

    def test_schema_release_impact_uses_product_version(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "development").mkdir()
            version_file = root / schema_change.PRODUCT_VERSION_PATH
            with (
                patch.object(schema_change, "load_base_schemas", return_value={}),
                patch.object(schema_change, "load_current_schemas", return_value={}),
                patch.object(schema_change, "required_bump", return_value="minor"),
                patch.object(
                    schema_change,
                    "run_git",
                    return_value="project('opk', version: '1.2.3')\n",
                ),
            ):
                version_file.write_text(
                    "project('opk', version: '1.2.4')\n", encoding="utf-8"
                )
                insufficient = schema_change.evaluate(root, "base")
                self.assertEqual(insufficient["required_bump"], "minor")
                self.assertTrue(
                    any(
                        finding["severity"] == "error"
                        for finding in insufficient["findings"]
                    )
                )

                version_file.write_text(
                    "project('opk', version: '1.3.0')\n", encoding="utf-8"
                )
                sufficient = schema_change.evaluate(root, "base")
                self.assertFalse(
                    any(
                        finding["severity"] == "error"
                        for finding in sufficient["findings"]
                    )
                )


class SemanticVersionTests(unittest.TestCase):
    def test_accepts_stable_semantic_version(self) -> None:
        release_package.require_semantic_version("1.2.3")

    def test_rejects_non_release_versions(self) -> None:
        for version in ("1.2", "v1.2.3", "1.2.3-rc1", "1.2.3+build"):
            with self.subTest(version=version), self.assertRaises(RuntimeError):
                release_package.require_semantic_version(version)

    def test_repository_commit_is_accepted_alone(self) -> None:
        args = release_package.parse_args(["package", "--repository-commit", "a" * 40])
        self.assertEqual(args.repository_commit, "a" * 40)
        self.assertFalse(hasattr(args, "flowdata_commit"))

    def test_flowdata_commit_flag_is_removed(self) -> None:
        with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            release_package.parse_args(["package", "--flowdata-commit", "a" * 40])

    def test_rejects_invalid_repository_commit_before_packaging(self) -> None:
        for commit in ("", "abc123", "g" * 40, "a" * 39, "a" * 41, "a" * 40 + "\n"):
            with self.subTest(commit=commit), patch.object(
                release_package.perception_generate, "verify_perception_manifest"
            ) as verify:
                args = release_package.parse_args(["--repository-commit", commit])
                with self.assertRaisesRegex(RuntimeError, "full Git SHA"):
                    release_package.build_bundle(args)
                verify.assert_not_called()


class SdkDescriptorTests(unittest.TestCase):
    def test_descriptor_and_product_version_are_the_release_configuration(self) -> None:
        config = release_package.perception_config.load_sdk_config()
        descriptor = json.loads(config.descriptor_path.read_text(encoding="utf-8"))
        self.assertEqual(config.name, descriptor["name"])
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
            [artifact.name for artifact in config.flatbuffers_rust_crates],
            ["flatbuffers", "bitflags", "rustc_version", "semver"],
        )
        self.assertEqual(config.flatbuffers_rust_crates[0].version, config.flatbuffers_version)
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
            *config.flatbuffers_rust_crates,
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
            config.flowdata_root.relative_to(release_package.REPO_ROOT).as_posix(),
            descriptor["flowdata_sdk"]["root"],
        )
        self.assertEqual(set(descriptor["flowdata_sdk"]), {"root", "generator"})
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
            meson.write_text("project('demo', version: 'next')\n", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "stable MAJOR.MINOR.PATCH"):
                release_package.perception_config.product_version(meson)

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

    def test_descriptor_does_not_read_gitmodules(self) -> None:
        original = Path.open

        def open_without_gitmodules(path, *args, **kwargs):
            self.assertNotEqual(path.name, ".gitmodules")
            return original(path, *args, **kwargs)

        with patch.object(Path, "open", open_without_gitmodules):
            release_package.perception_config.load_sdk_config()

    def test_descriptor_rejects_submodule_and_escaping_paths(self) -> None:
        descriptor = json.loads(
            release_package.perception_config.SDK_CONFIG_PATH.read_text(encoding="utf-8")
        )
        for flowdata in (
            {"submodule": "tools/flowdata-sdk", "generator": "tools/flowdata/gen.py"},
            {"root": "../outside", "generator": "gen.py"},
            {"root": "/outside", "generator": "gen.py"},
            {"root": "tools/flowdata-sdk", "generator": "../gen.py"},
            {"root": "tools/flowdata-sdk", "generator": "/gen.py"},
        ):
            with self.subTest(flowdata=flowdata), tempfile.TemporaryDirectory() as tmp:
                path = Path(tmp) / "sdk.json"
                descriptor["flowdata_sdk"] = flowdata
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
    def test_prerelease_package_versions_keep_the_sdk_version(self) -> None:
        config = replace(
            release_package.perception_config.load_sdk_config(),
            version="20260914.1123456.34831718470001",
            package_prerelease=True,
        )
        self.assertEqual(
            config.python_package_version,
            "20260914.1123456.34831718470001.dev0",
        )
        self.assertEqual(
            config.cargo_package_version,
            "20260914.1123456.34831718470001-dev.0",
        )

    def test_generator_sets_the_single_product_version_source(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            meson = Path(tmp) / "meson.build"
            meson.write_text(
                "project('demo', version: '0.3.0')\n", encoding="utf-8"
            )
            release_package.perception_generate.set_product_version(
                meson, "20260910.1091234.34458724856"
            )
            self.assertEqual(
                meson.read_text(encoding="utf-8"),
                "project('demo', version: '20260910.1091234.34458724856')\n",
            )

    def test_synchronizes_plumber_dependency_with_the_product_version(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            project = Path(tmp) / "pyproject.toml"
            project.write_text(
                '[project]\ndependencies = [\n    "opk-perception-sdk==0.3.0",\n]\n',
                encoding="utf-8",
            )

            with redirect_stdout(io.StringIO()):
                self.assertFalse(
                    release_package.perception_generate.synchronize_plumber_dependency(
                        project, "0.0.4309101", True
                    )
                )
            self.assertTrue(
                release_package.perception_generate.synchronize_plumber_dependency(
                    project, "0.0.4309101", False
                )
            )
            self.assertIn("opk-perception-sdk==0.0.4309101", project.read_text())

    def test_typescript_declaration_headers_are_idempotent(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            generated = Path(tmp)
            declaration = generated / "ts/dist/perception/index.d.ts"
            declaration.parent.mkdir(parents=True)
            declaration.write_text("export {};\n", encoding="utf-8")

            for _ in range(2):
                release_package.perception_generate.add_typescript_declaration_headers(
                    generated
                )

            expected_header = (
                release_package.perception_generate.TS_LICENSE_HEADER
                + release_package.perception_generate.TS_GENERATED_HEADER
            )
            self.assertEqual(
                declaration.read_text(encoding="utf-8"),
                f"{expected_header}export {{}};\n",
            )


class LocalSourceReceiptTests(unittest.TestCase):
    def setUp(self) -> None:
        self.generate = release_package.perception_generate
        temporary = tempfile.TemporaryDirectory(
            prefix=".perception-source-test-", dir=release_package.REPO_ROOT
        )
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        config = release_package.perception_config.load_sdk_config()
        flowdata_root = self.root / "tools/flowdata-sdk"
        self.tree = flowdata_root / "tools/flowdata"
        for relative in (
            "gen.py", "engine/__init__.py", "engine/app.py", "engine/manifest.py",
            "engine/version.py", "engine/generators/extra.py", ".hidden/module.py",
        ):
            path = self.tree / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                'GENERATOR_VERSION = "0.6.0"\n' if relative == "engine/version.py"
                else 'raise AssertionError("generator must not execute")\n',
                encoding="utf-8",
            )
        schema_dir = self.root / "schemas"
        schema_dir.mkdir()
        (schema_dir / "payload.fbs").write_text("table Payload {}\n", encoding="utf-8")
        descriptor = self.root / "sdk.json"
        shutil.copyfile(config.descriptor_path, descriptor)
        generated = self.root / "generated"
        generated.mkdir()
        internal = self.root / "meson.build"
        internal.write_text("# integration\n", encoding="utf-8")
        self.config = replace(
            config, flowdata_root=flowdata_root, flowdata_generator=self.tree / "gen.py",
            descriptor_path=descriptor, generated_root=generated,
            internal_meson_path=internal, schema_dir=schema_dir,
        )
        self.identity = self.generate.flowdata_source_identity(self.config)
        receipts = {
            sdk: {
                "sdk": {"name": config.name, "version": config.version},
                "generator": self.identity["generator"],
                "flatc": {"semantic_version": config.flatbuffers_version},
                "schema_files": self.generate._schema_records(schema_dir),
                "schema_set_sha256": self.generate._schema_set_sha256(schema_dir),
                "payloads": [],
                "outputs": {
                    "sdk": sdk, "cpp_python_bridge": sdk == "cpp",
                    "integrations": ["cmake", "meson"] if sdk == "cpp" else [],
                },
            }
            for sdk in ("cpp", "python", "rust", "ts")
        }
        receipts["python"]["python_package"] = {
            "distribution_name": release_package.PYTHON_DISTRIBUTION_NAME,
        }
        with (
            patch.object(self.generate, "command_version", return_value="test formatter"),
            patch.object(self.generate, "rustfmt_version", return_value="rustfmt 1.8.0"),
        ):
            self.generate.write_perception_manifest(
                self.config, generated, internal, receipts, "clang-format", "python", "node"
            )
        self.manifest_path = generated / self.generate.PERCEPTION_MANIFEST_FILENAME
        self.manifest = json.loads(self.manifest_path.read_text(encoding="utf-8"))

    def test_records_all_local_python_sources_without_git_or_execution(self) -> None:
        self.assertFalse((self.config.flowdata_root / ".git").exists())
        self.assertFalse((self.root / ".gitmodules").exists())
        with patch.object(subprocess, "run", side_effect=AssertionError("no commands")):
            verified = self.generate.verify_perception_manifest(self.config)
        self.assertEqual(verified["generation"]["flowdata_sdk"], self.identity)
        self.assertEqual(set(self.identity), {"generator", "sources"})
        self.assertEqual(self.identity["generator"], {"name": "flowdata-sdk", "version": "0.6.0"})
        self.assertEqual(
            self.identity["sources"],
            [
                {"path": path.relative_to(self.config.flowdata_root).as_posix(),
                 "sha256": digest(path), "size": path.stat().st_size}
                for path in sorted(self.tree.rglob("*.py"))
            ],
        )
        self.assertEqual(verified["upstream_receipts"], self.manifest["upstream_receipts"])

    def test_source_edits_missing_and_extra_modules_require_regeneration(self) -> None:
        module = self.tree / "engine/generators/extra.py"
        original = module.read_bytes()
        for mutation in ("modified", "missing", "extra", "version"):
            with self.subTest(mutation=mutation):
                version = self.tree / "engine/version.py"
                version_text = version.read_bytes()
                extra = self.tree / "new.py"
                try:
                    if mutation == "modified":
                        module.write_bytes(original + b"\n")
                    elif mutation == "missing":
                        module.unlink()
                    elif mutation == "extra":
                        extra.write_text("# new module\n", encoding="utf-8")
                    else:
                        version.write_text('GENERATOR_VERSION = "0.6.1"\n', encoding="utf-8")
                    with self.assertRaisesRegex(RuntimeError, "flowdata-sdk changed"):
                        self.generate.verify_perception_manifest(self.config)
                finally:
                    module.write_bytes(original)
                    version.write_bytes(version_text)
                    extra.unlink(missing_ok=True)

    def test_ignores_only_python_cache_directories(self) -> None:
        cache = self.tree / "engine/__pycache__"
        cache.mkdir()
        (cache / "ignored.py").write_text("# cache\n", encoding="utf-8")
        self.generate.verify_perception_manifest(self.config)
        hidden = self.tree / ".hidden/module.py"
        hidden.write_text("# changed\n", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "flowdata-sdk changed"):
            self.generate.verify_perception_manifest(self.config)

    def test_rejects_missing_essential_sources(self) -> None:
        for relative in ("gen.py", "engine/app.py", "engine/__init__.py",
                         "engine/version.py", "engine/manifest.py"):
            with self.subTest(relative=relative):
                path = self.tree / relative
                content = path.read_bytes()
                path.unlink()
                try:
                    with self.assertRaisesRegex(RuntimeError, "tracked flowdata-sdk.*missing"):
                        self.generate.verify_perception_manifest(self.config)
                finally:
                    path.write_bytes(content)

    def test_rejects_symlinked_files_directories_and_vendor_root(self) -> None:
        for target in (self.tree / "gen.py", self.tree / "engine", self.root / "absent"):
            with self.subTest(target=target):
                link = self.tree / "link"
                link.symlink_to(target)
                try:
                    with self.assertRaisesRegex(RuntimeError, "symlinks"):
                        self.generate.verify_perception_manifest(self.config)
                finally:
                    link.unlink()
        alias = self.root / "alias"
        alias.symlink_to(self.config.flowdata_root, target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError, "symlinks"):
            self.generate.flowdata_source_identity(replace(
                self.config, flowdata_root=alias,
                flowdata_generator=alias / "tools/flowdata/gen.py",
            ))

    def test_rejects_escaping_generator(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "inside the repository"):
            self.generate.flowdata_source_identity(replace(
                self.config, flowdata_generator=self.root / "outside.py"
            ))

    def test_version_is_static_and_semantic(self) -> None:
        for value in ('"next"', '"0.6.0" + ""', '__import__("missing_module")'):
            with self.subTest(value=value):
                (self.tree / "engine/version.py").write_text(
                    f"GENERATOR_VERSION = {value}\n", encoding="utf-8"
                )
                with self.assertRaisesRegex(RuntimeError, "literal GENERATOR_VERSION"):
                    self.generate.flowdata_source_identity(self.config)

    def test_rejects_old_commit_receipt(self) -> None:
        self.manifest["generation"]["flowdata_sdk"] = {
            "commit": "a" * 40, "generator": self.identity["generator"],
        }
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "flowdata-sdk changed"):
            self.generate.verify_perception_manifest(self.config)

    def test_rejects_descriptor_receipt_mismatch(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "descriptor identity"):
            self.generate.verify_perception_manifest(replace(self.config, descriptor_sha256="0" * 64))

    def test_rejects_schema_changes_without_regeneration(self) -> None:
        (self.config.schema_dir / "payload.fbs").write_text("table Changed {}\n", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "schema inputs are stale"):
            self.generate.verify_perception_manifest(self.config)

    def test_all_raw_receipts_must_identify_the_local_generator(self) -> None:
        for sdk in ("cpp", "python", "rust", "ts"):
            with self.subTest(sdk=sdk):
                manifest = json.loads(self.manifest_path.read_text(encoding="utf-8"))
                manifest["upstream_receipts"][sdk]["generator"]["version"] = "0.5.0"
                with self.assertRaisesRegex(RuntimeError, "generator identity is stale"):
                    self.generate._verify_upstream_receipts(self.config, manifest, self.identity)
        receipts = json.loads(json.dumps(self.manifest["upstream_receipts"]))
        for receipt in receipts.values():
            receipt["generator"]["version"] = "0.5.0"
        with self.assertRaisesRegex(RuntimeError, "does not match local sources"):
            self.generate.validate_flowdata_manifests(self.config, receipts)

    def test_git_free_packaging_still_validates_local_sources(self) -> None:
        args = release_package.parse_args(["--repository-commit", "a" * 40])
        with (
            patch.object(release_package.perception_config, "load_sdk_config", return_value=self.config),
            patch.object(subprocess, "run", side_effect=AssertionError("no commands")),
            patch.object(shutil, "copytree", side_effect=RuntimeError("packaging reached")),
        ):
            with self.assertRaisesRegex(RuntimeError, "packaging reached"):
                release_package.build_bundle(args)
            (self.tree / "new.py").write_text("# extra\n", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "flowdata-sdk changed"):
                release_package.build_bundle(args)

    def test_checkout_packaging_preserves_dirty_check(self) -> None:
        args = release_package.parse_args([])
        with (
            patch.object(release_package.perception_config, "load_sdk_config", return_value=self.config),
            patch.object(release_package, "git_commit", return_value="a" * 40) as commit,
            patch.object(release_package, "repository_git_status", return_value=" M sdk.json") as status,
            patch.object(shutil, "copytree", side_effect=RuntimeError("packaging reached")),
        ):
            with self.assertRaisesRegex(RuntimeError, "inputs or outputs are dirty"):
                release_package.build_bundle(args)
            commit.assert_called_once_with()
            status.assert_called_once_with()
            args.allow_dirty = True
            with self.assertRaisesRegex(RuntimeError, "packaging reached"):
                release_package.build_bundle(args)


class PythonPackagingTests(unittest.TestCase):
    def test_generated_distribution_name_is_rewritten_once(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            project = Path(tmp)
            pyproject = project / "pyproject.toml"
            pyproject.write_text(
                '[project]\nname = "perception"\nversion = "1.2.3"\n',
                encoding="utf-8",
            )

            release_package.perception_generate.prepare_python_package(
                project, "perception", "1.2.3", "1.2.3.dev0"
            )
            self.assertEqual(
                pyproject.read_text(encoding="utf-8"),
                '[project]\nname = "opk-perception-sdk"\nversion = "1.2.3.dev0"\n'
                'license = "Apache-2.0"\nlicense-files = ["LICENSE", "NOTICE"]\n',
            )
            with self.assertRaisesRegex(RuntimeError, "project name is unexpected"):
                release_package.perception_generate.prepare_python_package(
                    project, "perception", "1.2.3", "1.2.3.dev0"
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
        for legal in release_package.perception_generate.SDK_LEGAL_FILES:
            shutil.copyfile(release_package.perception_generate.SDK_LEGAL_INPUT_DIR / legal, source / legal)
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
        generator = {
            "generator": {"name": "flowdata-sdk", "version": "0.6.0"},
            "sources": [{"path": "tools/flowdata/gen.py", "sha256": "0" * 64, "size": 10}],
        }
        flatbuffers_wheel_path = bundle / "python/flatbuffers.whl"
        flatbuffers_typescript_path = bundle / "typescript/flatbuffers-25.9.23.tgz"
        generated_rust_files = {
            "rust/Cargo.toml",
            "rust/src/lib.rs",
        }
        files = {
            "cpp/perception.h": b"header\n",
            "metadata/perception-sdk-manifest.json": b"{}\n",
            "rust/Cargo.toml": (
                b'[package]\nname = "perception"\nversion = "1.2.3"\n'
                b'edition = "2021"\n\n[dependencies]\nflatbuffers = "=25.9.23"\n'
            ),
            "rust/src/lib.rs": b"pub struct Envelope;\n",
            "schemas/payload.fbs": b"namespace perception.metadata;\n",
        }
        rust_crates = [
            ("flatbuffers", "25.9.23"),
            ("bitflags", "2.13.1"),
            ("rustc_version", "0.4.1"),
            ("semver", "1.0.28"),
        ]
        rust_crate_records = []
        lock_packages = []
        for name, version in rust_crates:
            filename = f"{name}-{version}.crate"
            relative = f"rust/crates/{filename}"
            crate_file = b"[package]\n"
            archive_bytes = io.BytesIO()
            with tarfile.open(  # NOSONAR
                fileobj=archive_bytes, mode="w:gz"
            ) as archive:
                member = tarfile.TarInfo(f"{name}-{version}/Cargo.toml")
                member.size = len(crate_file)
                archive.addfile(member, io.BytesIO(crate_file))
            files[relative] = archive_bytes.getvalue()
            checksum = hashlib.sha256(files[relative]).hexdigest()
            vendor_cargo = f"rust/vendor/{name}-{version}/Cargo.toml"
            files[vendor_cargo] = crate_file
            vendor_checksum = f"rust/vendor/{name}-{version}/.cargo-checksum.json"
            files[vendor_checksum] = json.dumps(
                {
                    "files": {"Cargo.toml": hashlib.sha256(crate_file).hexdigest()},
                    "package": checksum,
                },
                sort_keys=True,
            ).encode()
            rust_crate_records.append({
                "filename": filename,
                "name": name,
                "path": relative,
                "sha256": checksum,
                "url": f"https://example.invalid/{filename}",
                "version": version,
            })
            lock_packages.append(
                f'[[package]]\nname = "{name}"\nversion = "{version}"\n'
                f'source = "registry+https://github.com/rust-lang/crates.io-index"\n'
                f'checksum = "{checksum}"\n'
            )
        files["rust/.cargo/config.toml"] = release_package.RUST_CARGO_CONFIG.encode()
        files["rust/Cargo.lock"] = (
            "# This file is automatically @generated by Cargo.\n"
            "version = 4\n\n"
            + "\n".join(lock_packages)
            + '\n[[package]]\nname = "perception"\nversion = "1.2.3"\n'
        ).encode()
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
                "version": "1.0.0",
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
                    "rust_crates": [
                        {
                            key: record[key]
                            for key in ("filename", "name", "sha256", "url", "version")
                        }
                        for record in rust_crate_records
                    ],
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
                "generation": {"flowdata_sdk": generator},
                "descriptor": {
                    "path": "tools/perception/sdk.json",
                    "sha256": descriptor_sha256,
                },
                "files": [
                    {
                        "path": relative,
                        "sha256": digest(bundle / relative),
                        "size": (bundle / relative).stat().st_size,
                    }
                    for relative in sorted(files)
                    if relative in generated_rust_files
                ],
                "upstream_receipts": {
                    "cpp": {
                        "schema_files": schema_files,
                        "schema_set_sha256": schema_digest,
                    },
                    "rust": {
                        "flatbuffers_runtimes": [
                            {
                                "language": "rust",
                                "package": "flatbuffers",
                                "version_requirement": "==25.9.23",
                            }
                        ],
                        "files": [
                            {
                                "path": relative.removeprefix("rust/"),
                                "sha256": digest(bundle / relative),
                                "size": (bundle / relative).stat().st_size,
                            }
                            for relative in sorted(files)
                            if relative in generated_rust_files
                        ],
                        "outputs": {"sdk": "rust"},
                    },
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
            "generator": generator,
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
                "rust_crates": rust_crate_records,
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
                "runtimes": [
                    {
                        "language": "rust",
                        "package": "flatbuffers",
                        "version_requirement": "==25.9.23",
                    }
                ],
            },
            "outputs": {
                "cpp": {
                    "cpp_python_bridge": True,
                    "integrations": ["cmake", "meson"],
                    "sdk": "cpp",
                },
                "python": {"sdk": "python"},
                "rust": {"sdk": "rust"},
                "typescript": {"sdk": "ts"},
                "python_bridge": {},
                "python_package": {
                    "distribution_name": "opk-perception-sdk",
                    "import_name": "perception",
                    "version": "1.2.3",
                },
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
            with tarfile.open(first, "r:gz") as archive:
                for legal in release_package.perception_generate.SDK_LEGAL_FILES:
                    with archive.extractfile(f"package/{legal}") as content:
                        self.assertEqual(
                            content.read(),
                            (release_package.perception_generate.SDK_LEGAL_INPUT_DIR / legal).read_bytes(),
                        )
            self.assertEqual(
                release_package.npm_package_metadata(first)["name"], "perception"
            )

    def test_rejects_oversized_npm_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "source"
            source.mkdir()
            for legal in release_package.perception_generate.SDK_LEGAL_FILES:
                shutil.copyfile(release_package.perception_generate.SDK_LEGAL_INPUT_DIR / legal, source / legal)
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

    def test_rejects_generator_identity_mismatch(self) -> None:
        for field in ("generator", "sources"):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as tmp:
                bundle = self.create_bundle(Path(tmp))
                path = bundle / release_package.MANIFEST_FILENAME
                manifest = json.loads(path.read_text(encoding="utf-8"))
                manifest["generator"][field] = []
                path.write_text(json.dumps(manifest), encoding="utf-8")
                with self.assertRaisesRegex(RuntimeError, "generator identity.*generation receipt"):
                    release_package.verify_bundle(bundle)

    def test_verifies_consistent_historical_bundle_generator_identity(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            bundle = self.create_bundle(Path(tmp))
            path = bundle / "metadata/perception-sdk-manifest.json"
            generated = json.loads(path.read_text(encoding="utf-8"))
            identity = {
                "commit": "a" * 40,
                "generator": {"name": "flowdata-sdk", "version": "0.5.0"},
            }
            generated["generation"]["flowdata_sdk"] = identity
            path.write_text(json.dumps(generated), encoding="utf-8")
            path = bundle / release_package.MANIFEST_FILENAME
            manifest = json.loads(path.read_text(encoding="utf-8"))
            manifest["generator"] = identity
            path.write_text(json.dumps(manifest), encoding="utf-8")
            descriptor = json.loads((bundle / "metadata/sdk.json").read_text(encoding="utf-8"))
            self.rewrite_descriptor_identity(bundle, descriptor)
            release_package.verify_bundle(bundle)

    def test_rejects_modified_rust_crate(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            bundle = self.create_bundle(Path(tmp))
            (bundle / "rust" / "src" / "lib.rs").write_text(
                "pub struct Changed;\n", encoding="utf-8"
            )
            with self.assertRaises(RuntimeError):
                release_package.verify_bundle(bundle)

    def test_rejects_modified_rust_vendor_contents(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            bundle = self.create_bundle(Path(tmp))
            vendor = bundle / "rust/vendor/flatbuffers-25.9.23/Cargo.toml"
            vendor.write_text("[package]\nname = \"changed\"\n", encoding="utf-8")
            manifest_path = bundle / release_package.MANIFEST_FILENAME
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            record = next(
                entry for entry in manifest["files"]
                if entry["path"] == "rust/vendor/flatbuffers-25.9.23/Cargo.toml"
            )
            record["sha256"] = digest(vendor)
            record["size"] = vendor.stat().st_size
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            with self.assertRaisesRegex(RuntimeError, "vendor contents do not match"):
                release_package.verify_bundle(bundle)

    def test_rejects_unsafe_rust_crate_path(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            crate = root / "flatbuffers-25.9.23.crate"
            with tarfile.open(  # NOSONAR
                crate, "w:gz"
            ) as archive:
                content = b"unsafe\n"
                member = tarfile.TarInfo("flatbuffers-25.9.23/../outside")
                member.size = len(content)
                archive.addfile(member, io.BytesIO(content))
            artifact = release_package.perception_config.LockedArtifact(
                name="flatbuffers",
                version="25.9.23",
                filename=crate.name,
                url=f"https://example.invalid/{crate.name}",
                sha256=digest(crate),
            )
            with self.assertRaisesRegex(RuntimeError, "unsafe path"):
                release_package.extract_rust_crate(crate, root / "vendor", artifact)

    def test_rejects_duplicate_rust_crate_path(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            crate = root / "flatbuffers-25.9.23.crate"
            with tarfile.open(  # NOSONAR
                crate, "w:gz"
            ) as archive:
                for content in (b"first\n", b"second\n"):
                    member = tarfile.TarInfo("flatbuffers-25.9.23/src/lib.rs")
                    member.size = len(content)
                    archive.addfile(member, io.BytesIO(content))
            artifact = release_package.perception_config.LockedArtifact(
                name="flatbuffers",
                version="25.9.23",
                filename=crate.name,
                url=f"https://example.invalid/{crate.name}",
                sha256=digest(crate),
            )
            with self.assertRaisesRegex(RuntimeError, "duplicate archive path"):
                release_package.extract_rust_crate(crate, root / "vendor", artifact)

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

    def test_rejects_python_distribution_metadata_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            bundle = self.create_bundle(Path(tmp))
            manifest_path = bundle / release_package.MANIFEST_FILENAME
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["outputs"]["python_package"]["distribution_name"] = "perception"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            with self.assertRaisesRegex(RuntimeError, "Python package identity"):
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

    def test_rejects_descriptor_rust_crate_lock_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            bundle = self.create_bundle(Path(tmp))
            descriptor_path = bundle / "metadata/sdk.json"
            descriptor = json.loads(descriptor_path.read_text(encoding="utf-8"))
            descriptor["flatbuffers"]["rust_crates"][0]["sha256"] = "2" * 64
            self.rewrite_descriptor_identity(bundle, descriptor)

            with self.assertRaisesRegex(RuntimeError, "Rust crate lock 0"):
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
    def test_release_readme_bootstraps_consumer_lockfile(self) -> None:
        config = release_package.perception_config.load_sdk_config()
        with tempfile.TemporaryDirectory() as tmp:
            bundle_root = Path(tmp)
            release_package.write_readme(bundle_root, config)
            readme = (bundle_root / "README.md").read_text(encoding="utf-8")

        generate = readme.index("cargo generate-lockfile --offline")
        build = readme.index("cargo build --offline --locked")
        self.assertLess(generate, build)

    def test_release_copy_excludes_rust_build_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "source"
            (source / "src").mkdir(parents=True)
            (source / "target" / "debug").mkdir(parents=True)
            (source / "Cargo.toml").write_text("[package]\n", encoding="utf-8")
            (source / "Cargo.lock").write_text("transient\n", encoding="utf-8")
            (source / "src" / "lib.rs").write_text("pub fn sdk() {}\n", encoding="utf-8")
            (source / "target" / "debug" / "sdk").write_bytes(b"transient")

            destination = root / "destination"
            release_package.copy_rust_sdk(source, destination)

            self.assertTrue((destination / "Cargo.toml").is_file())
            self.assertTrue((destination / "src" / "lib.rs").is_file())
            self.assertFalse((destination / "Cargo.lock").exists())
            self.assertFalse((destination / "target").exists())

    @unittest.skipUnless(cargo_is_usable(), "cargo is not installed or usable")
    def test_generated_rust_sdk(self) -> None:
        config = release_package.perception_config.load_sdk_config()
        with tempfile.TemporaryDirectory() as tmp:
            rust_copy = Path(tmp) / "rust"
            shutil.copytree(config.generated_root / "rust", rust_copy)
            subprocess.run(
                ["cargo", "test", "--manifest-path", str(rust_copy / "Cargo.toml")],
                check=True,
            )

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
        self.assertTrue((generated / "rust" / "Cargo.toml").is_file())
        self.assertTrue((generated / "rust" / "src" / "lib.rs").is_file())
        self.assertTrue((generated / "rust" / "tests" / "opk_packet.rs").is_file())
        self.assertIn(
            f'name = "{config.name}"',
            (generated / "rust" / "Cargo.toml").read_text(encoding="utf-8"),
        )

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
        self.assertNotIn("manifest_version", manifest["upstream_receipts"]["rust"])
        self.assertNotIn("manifest_version", manifest["upstream_receipts"]["ts"])
        self.assertEqual(manifest["upstream_receipts"]["rust"]["outputs"]["sdk"], "rust")
        self.assertEqual(manifest["upstream_receipts"]["ts"]["outputs"]["sdk"], "ts")
        self.assertEqual(
            manifest["upstream_receipts"]["python"]["python_package"]["distribution_name"],
            release_package.perception_config.PYTHON_DISTRIBUTION_NAME,
        )
        self.assertEqual(
            manifest["postprocessing"]["typescript"]["flatbuffers_runtime"],
            config.typescript_runtime.version,
        )
        rust_postprocessing = manifest["postprocessing"]["rust"]
        self.assertEqual(
            rust_postprocessing["flatbuffers_runtime"], config.flatbuffers_version
        )
        self.assertRegex(
            rust_postprocessing["formatter"], r"^rustfmt \d+\.\d+\.\d+$"
        )
        self.assertIs(rust_postprocessing["standard_library"], True)
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
