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
    for model_id in release_tool.RELEASE_MODEL_NAMES:
        add_model(repo_root, model_id, ONNX_MODEL_FILE, ONNX_INFERENCE_OP)


class ReleaseToolTests(unittest.TestCase):
    def run_tool(self, *arguments: str, cwd: Path = REPO_ROOT) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["python3", str(TOOL), *arguments],
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

    def test_stages_local_model_with_relative_references(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "config/opchains").mkdir(parents=True)
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
            (root / "config/opchains").mkdir(parents=True)
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

    def test_rejects_hailo_release_content(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            package_root = Path(temporary)
            plugin_root = package_root / "lib/gstreamer-1.0"
            private_root = package_root / "lib/pek"
            plugin_root.mkdir(parents=True)
            private_root.mkdir()
            for plugin_name in release_tool.PLUGIN_NAMES:
                (plugin_root / plugin_name).touch()
            (private_root / "pek-hailort-ops.so").touch()
            with (
                patch.object(
                    release_tool,
                    "is_elf",
                    side_effect=lambda path: path.name in release_tool.PLUGIN_NAMES,
                ),
                self.assertRaisesRegex(RuntimeError, "Forbidden Hailo release path"),
            ):
                release_tool.validate_runtime_files(package_root)

    def test_rejects_incomplete_model_allowlist(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            package_root = Path(temporary)
            plugin_root = package_root / "lib/gstreamer-1.0"
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
            plugin_root = package_root / "lib/gstreamer-1.0"
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

    def test_validates_selected_model_and_opchain_payload(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo_root = root / "source"
            shared_root = repo_root / "config/opchains/tracking"
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

    def test_only_final_preparation_requires_matching_changelog(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            development_root = root / "development"
            development_root.mkdir()
            (development_root / "meson.build").write_text(
                "project('demo', version: '0.1.0')\n", encoding="utf-8"
            )
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
            (root / "config/opchains").mkdir(parents=True)
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
