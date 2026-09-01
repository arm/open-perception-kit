#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

"""Focused checks for release model discovery and staging."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from scripts.release import ReleaseTool as release_tool  # noqa: E402

TOOL = Path(__file__).with_name("ReleaseTool.py")
REPO_ROOT = TOOL.parents[2]
MODEL_DESCRIPTOR = "model.json"
ONNX_MODEL_FILE = "model.onnx"
ONNX_INFERENCE_OP = "pek-onnx-ops/Inference"
EXECUTORCH_INFERENCE_OP = "pek-executorch-ops/Inference"
OPCHAINS_DIR = Path("config/opchains")
PLUGIN_DIR = Path("lib/gstreamer-1.0")
SOURCE_COMMIT = "a" * 40


class FakeDistribution:
    def __init__(self, root: Path, name: str, version: str, files: list[str]) -> None:
        self.root = root
        self.metadata = {"Name": name}
        self.version = version
        self.files = [Path(path) for path in files]

    def locate_file(self, path: object) -> Path:
        return self.root / str(path)


def add_model(
    repo_root: Path, model_id: str, filename: str, op_id: str, content: bytes = b"model"
) -> None:
    model_root = repo_root / "config/models" / model_id
    model_root.mkdir(parents=True)
    (model_root / filename).write_bytes(content)
    (model_root / MODEL_DESCRIPTOR).write_text(
        json.dumps({"modelFile": filename}), encoding="utf-8"
    )
    (model_root / "opchain.json").write_text(
        json.dumps(
            {
                "ops": [
                    {
                        "id": op_id,
                        "attributes": {"modelDescriptor": MODEL_DESCRIPTOR},
                    }
                ]
            }
        ),
        encoding="utf-8",
    )


def add_release_models(repo_root: Path) -> None:
    for model_id, (backend, suffix) in release_tool.RELEASE_MODELS.items():
        add_model(repo_root, model_id, f"model{suffix}", backend)
    schema_root = repo_root / "config/schemas/v1"
    schema_root.mkdir(parents=True)
    (schema_root / "model.schema.json").write_text("{}\n", encoding="utf-8")


def add_perception_sdk(
    root: Path,
    version: str = "0.1.0",
    commit: str = "a" * 40,
    dirty: bool = False,
) -> Path:
    root.mkdir(parents=True)
    archive = root / f"perception-sdk-{version}.zip"
    archive.write_bytes(b"sdk")
    (root / f"{archive.name}.sha256").write_text("checksum\n", encoding="utf-8")
    (root / f"{archive.name}.provenance.json").write_text(
        json.dumps({"dirty": dirty, "repository_commit": commit}), encoding="utf-8"
    )
    return archive


def add_release_identity(repo_root: Path) -> None:
    development_root = repo_root / "development"
    development_root.mkdir(parents=True)
    (development_root / "meson.build").write_text(
        "project('demo', version: '0.1.0')\n", encoding="utf-8"
    )


class ReleaseToolTests(unittest.TestCase):
    def run_tool(self, *arguments: str, cwd: Path = REPO_ROOT) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["python3", str(TOOL), *arguments],
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

    def test_stages_and_validates_private_python_runtime(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source_root = root / "site-packages"
            distributions = {
                "flatbuffers": FakeDistribution(
                    source_root,
                    "flatbuffers",
                    "25.9.23",
                    [
                        "flatbuffers/__init__.py",
                        "flatbuffers-25.9.23.dist-info/METADATA",
                        "flatbuffers-25.9.23.dist-info/RECORD",
                    ],
                ),
                "numpy": FakeDistribution(
                    source_root,
                    "numpy",
                    "2.4.2",
                    [
                        "numpy/__init__.py",
                        "numpy/_core/module.so",
                        "numpy/_core/include/numpy.h",
                        "numpy/tests/test_runtime.py",
                        "numpy-2.4.2.dist-info/METADATA",
                    ],
                ),
                "opk-perception-sdk": FakeDistribution(
                    source_root,
                    "opk_perception_sdk",
                    "0.3.0",
                    [
                        "perception/__init__.py",
                        "opk_perception_sdk-0.3.0.dist-info/METADATA",
                    ],
                ),
            }
            for distribution in distributions.values():
                for entry in distribution.files:
                    path = distribution.locate_file(entry)
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(str(entry), encoding="utf-8")

            stage_root = root / "stage"
            stale_runtime_root = stage_root / release_tool.PYTHON_RUNTIME_ROOT
            stale_runtime_root.mkdir(parents=True)
            (stale_runtime_root / "stale-package.py").write_text("stale", encoding="utf-8")
            with patch.object(
                release_tool.importlib.metadata,
                "distribution",
                side_effect=lambda name: distributions[name],
            ):
                release_tool.stage_python_runtime(SimpleNamespace(stage_root=str(stage_root)))

            runtime_root = stage_root / release_tool.PYTHON_RUNTIME_ROOT
            self.assertFalse((runtime_root / "stale-package.py").exists())
            (runtime_root / "pek_python_ops.pyi").write_text("", encoding="utf-8")
            self.assertTrue((runtime_root / "numpy/_core/module.so").is_file())
            self.assertFalse((runtime_root / "numpy/_core/include/numpy.h").exists())
            self.assertFalse((runtime_root / "numpy/tests/test_runtime.py").exists())
            self.assertFalse(
                (runtime_root / "flatbuffers-25.9.23.dist-info/RECORD").exists()
            )
            release_tool.validate_python_runtime(stage_root)

            repo_root = root / "source"
            (repo_root / "development/ops-python").mkdir(parents=True)
            (repo_root / "tools/perception").mkdir(parents=True)
            (repo_root / "generated/perception/python").mkdir(parents=True)
            (repo_root / "development/ops-python/runtime.json").write_text(
                json.dumps({"numpy": {"version": "2.4.2"}}), encoding="utf-8"
            )
            (repo_root / "tools/perception/sdk.json").write_text(
                json.dumps({"flatbuffers": {"version": "25.9.23"}}),
                encoding="utf-8",
            )
            (repo_root / "generated/perception/python/pyproject.toml").write_text(
                '[project]\nversion = "0.3.0"\n', encoding="utf-8"
            )
            release_tool.validate_python_runtime(stage_root, repo_root)

            (runtime_root / "requests").mkdir()
            with self.assertRaisesRegex(RuntimeError, "unexpected entries"):
                release_tool.validate_python_runtime(stage_root)
            (runtime_root / "requests").rmdir()

            (runtime_root / release_tool.PYTHON_RUNTIME_MANIFEST).write_text(
                json.dumps({"distributions": {"numpy": "2.4.2"}}),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(RuntimeError, "release contract"):
                release_tool.validate_python_runtime(stage_root)

    def test_elf_dependencies_can_resolve_through_rpath(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            package_root = Path(temporary)
            library_root = package_root / "share/pek/python/numpy.libs"
            library_root.mkdir(parents=True)
            extension = library_root / "libextension.so"
            dependency = library_root / "libdependency.so"
            extension.touch()
            dependency.touch()

            def fake_read_elf(path: Path, *arguments: str) -> str:
                if arguments == ("-hW",):
                    return "Machine: AArch64\n"
                if path == extension:
                    return """
 0x000000000000000f (RPATH) Library rpath: [$ORIGIN]
 0x0000000000000001 (NEEDED) Shared library: [libdependency.so]
"""
                return ""

            with patch.object(release_tool, "read_elf", side_effect=fake_read_elf):
                release_tool.validate_elf(
                    extension,
                    package_root,
                    "AArch64",
                    {dependency.name: [dependency]},
                )

    def test_stages_local_model_with_relative_references(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / OPCHAINS_DIR).mkdir(parents=True)
            add_release_models(root)
            model_root = root / "config/models/cam-contact"
            (model_root / "secondary.onnx").write_bytes(b"secondary")
            (model_root / "unreferenced.onnx").write_bytes(b"unreferenced")
            (model_root / "secondary.json").write_text(
                json.dumps({"modelFile": "secondary.onnx"}), encoding="utf-8"
            )
            stage_root = root / "stage"
            completed = self.run_tool(
                "stage-models",
                "--repo-root",
                str(root),
                "--stage-root",
                str(stage_root),
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            descriptor = json.loads(
                (stage_root / "share/pek/models/cam-contact/model.json").read_text(
                    encoding="utf-8"
                )
            )
            opchain = json.loads(
                (stage_root / "share/pek/models/cam-contact/opchain.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(descriptor["modelFile"], ONNX_MODEL_FILE)
            self.assertEqual(
                opchain["ops"][0]["attributes"]["modelDescriptor"], MODEL_DESCRIPTOR
            )
            self.assertTrue(
                (stage_root / "share/pek/models/cam-contact/secondary.json").is_file()
            )
            self.assertEqual(
                (stage_root / "share/pek/models/cam-contact/secondary.onnx").read_bytes(),
                b"secondary",
            )
            self.assertEqual(
                (stage_root / "share/pek/models/cam-contact/unreferenced.onnx").read_bytes(),
                b"unreferenced",
            )

    def test_stages_only_release_model_allowlist(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / OPCHAINS_DIR).mkdir(parents=True)
            add_release_models(root)
            add_model(root, "not-released", ONNX_MODEL_FILE, ONNX_INFERENCE_OP)
            stage_root = root / "stage"
            completed = self.run_tool(
                "stage-models",
                "--repo-root",
                str(root),
                "--stage-root",
                str(stage_root),
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertEqual(
                {
                    path.name
                    for path in (stage_root / "share/pek/models").iterdir()
                },
                release_tool.RELEASE_MODEL_NAMES,
            )

    def test_stages_executorch_model_bytes_and_backend(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / OPCHAINS_DIR).mkdir(parents=True)
            add_release_models(root)
            source_model = root / "config/models/yolox/model.pte"
            source_model.write_bytes(b"pte\x00payload")

            stage_root = root / "stage"
            release_tool.stage_models(
                SimpleNamespace(repo_root=str(root), stage_root=str(stage_root))
            )

            staged_model = stage_root / "share/pek/models/yolox/model.pte"
            self.assertEqual(staged_model.read_bytes(), b"pte\x00payload")
            opchain = json.loads(
                (stage_root / "share/pek/models/yolox/opchain.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(opchain["ops"][0]["id"], EXECUTORCH_INFERENCE_OP)

    def test_rejects_wrong_release_model_backend_or_suffix(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / OPCHAINS_DIR).mkdir(parents=True)
            add_release_models(root)
            yolox_opchain = root / "config/models/yolox/opchain.json"
            opchain = json.loads(yolox_opchain.read_text(encoding="utf-8"))
            opchain["ops"][0]["id"] = "pek-onnx-ops/Inference"
            yolox_opchain.write_text(json.dumps(opchain), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, EXECUTORCH_INFERENCE_OP):
                release_tool.discover_models(root)

            opchain["ops"][0]["id"] = EXECUTORCH_INFERENCE_OP
            yolox_opchain.write_text(json.dumps(opchain), encoding="utf-8")
            descriptor = root / "config/models/yolox/model.json"
            descriptor.write_text(json.dumps({"modelFile": "model.onnx"}), encoding="utf-8")
            (root / "config/models/yolox/model.onnx").write_bytes(b"onnx")
            with self.assertRaisesRegex(RuntimeError, "unsupported model file"):
                release_tool.discover_models(root)

    def test_rejects_retired_release_content(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            package_root = Path(temporary)
            for relative in (
                "lib/pek/libhailort.so",
                "share/pek/models/retired/model.hef",
            ):
                with self.subTest(relative=relative):
                    payload = package_root / relative
                    payload.parent.mkdir(parents=True, exist_ok=True)
                    payload.touch()
                    with self.assertRaisesRegex(
                        RuntimeError, "Forbidden retired release path"
                    ):
                        release_tool.validate_release_tree(package_root)
                    payload.unlink()

    def test_allows_source_named_legal_documentation_directories(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            package_root = Path(temporary)
            plugin_root = package_root / PLUGIN_DIR
            plugin_root.mkdir(parents=True)
            for plugin_name in release_tool.PLUGIN_NAMES:
                (plugin_root / plugin_name).touch()
            legal_root = package_root / "share/pek/licenses/libexecutorch-dev/examples"
            legal_root.mkdir(parents=True)
            (legal_root / "LICENSE").write_text("ExecuTorch", encoding="utf-8")

            with (
                patch.object(
                    release_tool,
                    "is_elf",
                    side_effect=lambda path: path.name in release_tool.PLUGIN_NAMES,
                ),
                self.assertRaisesRegex(RuntimeError, "Packaged model directory"),
            ):
                release_tool.validate_runtime_files(package_root)

    def test_rejects_incomplete_model_allowlist(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            package_root = Path(temporary)
            plugin_root = package_root / PLUGIN_DIR
            model_root = package_root / "share/pek/models"
            plugin_root.mkdir(parents=True)
            model_root.mkdir(parents=True)
            for plugin_name in release_tool.PLUGIN_NAMES:
                (plugin_root / plugin_name).touch()
            for model_name in sorted(release_tool.RELEASE_MODEL_NAMES)[:-1]:
                (model_root / model_name).mkdir()

            with (
                patch.object(
                    release_tool,
                    "is_elf",
                    side_effect=lambda path: path.name in release_tool.PLUGIN_NAMES,
                ),
                self.assertRaisesRegex(RuntimeError, "Packaged models must be exactly"),
            ):
                release_tool.validate_runtime_files(package_root)

    def test_rejects_missing_or_non_elf_runtime_modules(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            package_root = Path(temporary)
            plugin_root = package_root / PLUGIN_DIR
            private_root = package_root / "lib/pek"
            model_root = package_root / "share/pek/models"
            plugin_root.mkdir(parents=True)
            private_root.mkdir()
            model_root.mkdir(parents=True)
            for model_name in release_tool.RELEASE_MODEL_NAMES:
                (model_root / model_name).mkdir()
            plugin_names = sorted(release_tool.PLUGIN_NAMES)
            for plugin_name in plugin_names[:-1]:
                (plugin_root / plugin_name).touch()
            arguments = SimpleNamespace(
                architecture="x86_64", package_root=str(package_root)
            )

            with self.assertRaisesRegex(RuntimeError, "exactly six plugins"):
                release_tool.validate_package(arguments)

            (plugin_root / plugin_names[-1]).touch()
            with self.assertRaisesRegex(RuntimeError, "regular ELF files"):
                release_tool.validate_package(arguments)

            with (
                patch.object(
                    release_tool,
                    "is_elf",
                    side_effect=lambda path: path.name in release_tool.PLUGIN_NAMES,
                ),
                self.assertRaisesRegex(RuntimeError, "op modules must be exactly"),
            ):
                release_tool.validate_package(arguments)

            for module_name in release_tool.OP_MODULE_NAMES:
                (private_root / module_name).touch()
            with (
                patch.object(
                    release_tool,
                    "is_elf",
                    side_effect=lambda path: path.name in release_tool.PLUGIN_NAMES,
                ),
                self.assertRaisesRegex(RuntimeError, "op modules must be regular ELF"),
            ):
                release_tool.validate_package(arguments)

            runtime_elf_objects = release_tool.PLUGIN_NAMES | release_tool.OP_MODULE_NAMES
            with (
                patch.object(
                    release_tool,
                    "is_elf",
                    side_effect=lambda path: path.name in runtime_elf_objects,
                ),
                self.assertRaisesRegex(RuntimeError, "runtime library"),
            ):
                release_tool.validate_package(arguments)

    def test_requires_executorch_legal_documentation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            package_root = Path(temporary)
            legal_root = package_root / "share/pek/licenses"
            legal_root.mkdir(parents=True)
            (legal_root / "LICENSE").write_text("ONNX Runtime", encoding="utf-8")

            with self.assertRaisesRegex(RuntimeError, "ExecuTorch legal documentation"):
                release_tool.validate_legal_documentation(package_root)

            executorch_legal_root = legal_root / "libexecutorch-dev"
            executorch_legal_root.mkdir()
            (executorch_legal_root / "LICENSE").write_text(
                "ExecuTorch", encoding="utf-8"
            )
            release_tool.validate_legal_documentation(package_root)

    def test_validates_selected_model_and_opchain_payload(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo_root = root / "source"
            shared_root = repo_root / OPCHAINS_DIR / "tracking"
            shared_root.mkdir(parents=True)

            add_release_models(repo_root)
            (shared_root / "demo.json").write_text(
                json.dumps(
                    {
                        "ops": [
                            {
                                "id": ONNX_INFERENCE_OP,
                                "attributes": {
                                    "modelDescriptor": (
                                        "../../models/yolov11/model.json"
                                    )
                                },
                            },
                            {
                                "id": ONNX_INFERENCE_OP,
                                "attributes": {
                                    "modelDescriptor": (
                                        "/work/config/models/osnet_x0_25/model.json"
                                    )
                                },
                            }
                        ]
                    }
                ),
                encoding="utf-8",
            )

            package_root = root / "package"
            release_tool.stage_models(
                SimpleNamespace(
                    repo_root=str(repo_root),
                    stage_root=str(package_root),
                )
            )
            staged_opchain = json.loads(
                (
                    package_root / "share/pek/opchains/tracking/demo.json"
                ).read_text(encoding="utf-8")
            )
            self.assertEqual(
                [
                    op["attributes"]["modelDescriptor"]
                    for op in staged_opchain["ops"]
                ],
                [
                    "../../models/yolov11/model.json",
                    "../../models/osnet_x0_25/model.json",
                ],
            )
            release_tool.validate_release_payload(package_root, repo_root)

            model_path = package_root / "share/pek/models/yolov11" / ONNX_MODEL_FILE
            model = model_path.read_bytes()
            model_path.unlink()
            with self.assertRaisesRegex(RuntimeError, "models payload"):
                release_tool.validate_release_payload(package_root, repo_root)
            model_path.write_bytes(model)

            opchain_path = package_root / "share/pek/opchains/tracking/demo.json"
            opchain = opchain_path.read_bytes()
            opchain_path.unlink()
            with self.assertRaisesRegex(RuntimeError, "opchains payload"):
                release_tool.validate_release_payload(package_root, repo_root)
            opchain_path.write_bytes(opchain)

            model_opchain = package_root / "share/pek/models/yolov11/opchain.json"
            model_opchain.write_text(
                json.dumps(
                    {"modelDescriptor": "/work/config/models/yolov11/model.json"}
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(RuntimeError, "Build-machine path"):
                release_tool.validate_release_payload(package_root, repo_root)

    def test_stages_and_validates_descriptor_schemas(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo_root = root / "source"
            (repo_root / OPCHAINS_DIR).mkdir(parents=True)
            add_release_models(repo_root)
            nested_schema = repo_root / "config/schemas/v1/opchain/common.schema.json"
            nested_schema.parent.mkdir()
            nested_schema.write_bytes(b'{"title": "common"}\n')
            legacy_schema = repo_root / "metadata/api/perception.schema.json"
            legacy_schema.parent.mkdir(parents=True)
            legacy_schema.write_text("{}\n", encoding="utf-8")

            package_root = root / "package"
            release_tool.stage_models(
                SimpleNamespace(repo_root=str(repo_root), stage_root=str(package_root))
            )
            packaged_schema = (
                package_root / "share/pek/schemas/json/v1/opchain/common.schema.json"
            )
            self.assertEqual(packaged_schema.read_bytes(), nested_schema.read_bytes())
            self.assertFalse(
                (package_root / "share/pek/schemas/json/perception.schema.json").exists()
            )
            release_tool.validate_release_payload(package_root, repo_root)

            packaged_schema.write_text("{}\n", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "differs from the selected source"):
                release_tool.validate_release_payload(package_root, repo_root)
            packaged_schema.write_bytes(nested_schema.read_bytes())
            (packaged_schema.parent / "extra.json").write_text("{}\n", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "schemas/json/v1 payload"):
                release_tool.validate_release_payload(package_root, repo_root)
            (packaged_schema.parent / "extra.json").unlink()
            packaged_schema.unlink()
            with self.assertRaisesRegex(RuntimeError, "schemas/json/v1 payload"):
                release_tool.validate_release_payload(package_root, repo_root)

    def test_rejects_unsafe_descriptor_schema_trees(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            schema_root = root / "schemas"
            schema_root.mkdir()
            schema = schema_root / "model.schema.json"
            schema.write_text("{}\n", encoding="utf-8")
            release_tool.validate_schema_tree(schema_root)

            schema.write_text("{", encoding="utf-8")
            with self.assertRaises(json.JSONDecodeError):
                release_tool.validate_schema_tree(schema_root)
            schema.write_text("{}\n", encoding="utf-8")
            (schema_root / "README").write_text("not JSON", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "not JSON"):
                release_tool.validate_schema_tree(schema_root)
            (schema_root / "README").unlink()
            (schema_root / "linked.json").symlink_to(schema)
            with self.assertRaisesRegex(RuntimeError, "non-regular entry"):
                release_tool.validate_schema_tree(schema_root)

    def test_validates_perception_sdk_triplet(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdk_root = root / "perception-sdk"
            archive = add_perception_sdk(sdk_root)
            repo_root = root / "source"
            add_release_identity(repo_root)

            with patch.object(release_tool.subprocess, "run") as verifier:
                release_tool.validate_perception_sdk(
                    sdk_root, SOURCE_COMMIT, repo_root
                )
            verifier.assert_called_once_with(
                [
                    str(repo_root / "scripts/perception-sdk.sh"),
                    "verify",
                    str(archive),
                    "--require-sidecars",
                ],
                check=True,
                cwd=repo_root,
            )

            with patch.object(release_tool.subprocess, "run"):
                release_tool.validate_perception_sdk(sdk_root, SOURCE_COMMIT)

            (sdk_root / "extra").write_text("extra", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "matching triplet"):
                release_tool.validate_perception_sdk(sdk_root, SOURCE_COMMIT)
            (sdk_root / "extra").unlink()
            (sdk_root / f"{archive.name}.sha256").unlink()
            with self.assertRaisesRegex(RuntimeError, "matching triplet"):
                release_tool.validate_perception_sdk(sdk_root, SOURCE_COMMIT)

    def test_rejects_invalid_perception_sdk_provenance_and_entries(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdk_root = root / "perception-sdk"
            archive = add_perception_sdk(sdk_root, dirty=True)
            provenance = sdk_root / f"{archive.name}.provenance.json"

            with (
                patch.object(release_tool.subprocess, "run"),
                self.assertRaisesRegex(RuntimeError, "dirty=false"),
            ):
                release_tool.validate_perception_sdk(sdk_root, SOURCE_COMMIT)

            provenance.write_text(
                json.dumps({"dirty": False, "repository_commit": "invalid"}),
                encoding="utf-8",
            )
            with (
                patch.object(release_tool.subprocess, "run"),
                self.assertRaisesRegex(RuntimeError, "commit is invalid"),
            ):
                release_tool.validate_perception_sdk(sdk_root, SOURCE_COMMIT)

            provenance.unlink()
            provenance.symlink_to(archive)
            with self.assertRaisesRegex(RuntimeError, "regular files"):
                release_tool.validate_perception_sdk(sdk_root, SOURCE_COMMIT)

    def test_rejects_perception_sdk_selected_source_mismatch_and_verify_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdk_root = root / "perception-sdk"
            add_perception_sdk(sdk_root)
            repo_root = root / "source"
            add_release_identity(repo_root)
            (repo_root / "development/meson.build").write_text(
                "project('demo', version: '1.0.0')\n", encoding="utf-8"
            )

            with self.assertRaisesRegex(RuntimeError, "version does not match"):
                release_tool.validate_perception_sdk(
                    sdk_root, SOURCE_COMMIT, repo_root
                )

            (repo_root / "development/meson.build").write_text(
                "project('demo', version: '0.1.0')\n", encoding="utf-8"
            )
            with (
                patch.object(release_tool.subprocess, "run"),
                self.assertRaisesRegex(RuntimeError, "commit does not match"),
            ):
                release_tool.validate_perception_sdk(sdk_root, "b" * 40, repo_root)

            with (
                patch.object(
                    release_tool.subprocess,
                    "run",
                    side_effect=subprocess.CalledProcessError(1, "verify"),
                ),
                self.assertRaises(subprocess.CalledProcessError),
            ):
                release_tool.validate_perception_sdk(sdk_root, SOURCE_COMMIT)

    def test_only_final_preparation_requires_matching_changelog(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            add_release_identity(root)
            (root / "CHANGELOG.md").write_text("# Changelog\n", encoding="utf-8")
            arguments = (
                "prepare",
                "--repo-root",
                str(root),
                "--commit",
                "a" * 40,
            )

            manual = self.run_tool(*arguments, "--build-label", "test")
            self.assertEqual(manual.returncode, 0, manual.stderr)
            self.assertIn("build_id=0.1.0-test-aaaaaaaaaaaa", manual.stdout)

            final = self.run_tool(*arguments)
            self.assertNotEqual(final.returncode, 0)
            self.assertIn("no non-empty 0.1.0 section", final.stderr)

    def test_model_path_cannot_escape_its_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            model_root = root / "config/models/cam-contact"
            model_root.mkdir(parents=True)
            (root / OPCHAINS_DIR).mkdir(parents=True)
            (root / "config/models/escape.onnx").write_bytes(b"model")
            (model_root / MODEL_DESCRIPTOR).write_text(
                json.dumps({"modelFile": "../escape.onnx"}), encoding="utf-8"
            )
            (model_root / "opchain.json").write_text(
                json.dumps(
                    {
                        "ops": [
                            {
                                "id": ONNX_INFERENCE_OP,
                                "attributes": {"modelDescriptor": MODEL_DESCRIPTOR},
                            }
                        ]
                    }
                ),
                encoding="utf-8",
            )
            completed = self.run_tool(
                "stage-models",
                "--repo-root",
                str(root),
                "--stage-root",
                str(root / "stage"),
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertIn("safe relative path", completed.stderr)


if __name__ == "__main__":
    unittest.main()
