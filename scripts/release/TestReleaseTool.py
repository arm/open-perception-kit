#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

"""Focused checks for release package staging and validation."""

import json
import os
import shutil
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
ONNX_INFERENCE_OP = "opk-onnx-ops/Inference"
EXECUTORCH_INFERENCE_OP = "opk-executorch-ops/Inference"
OPCHAINS_DIR = Path("config/opchains")
PLUGIN_DIR = Path("lib/gstreamer-1.0")
SOURCE_COMMIT = "a" * 40
SOURCE_NOTICES = (
    ("jsoncons", "include/jsoncons/detail/grisu3.hpp"),
    ("nlohmann_json", "include/nlohmann/detail/conversions/to_chars.hpp"),
    ("nlohmann_json", "include/nlohmann/thirdparty/hedley/hedley.hpp"),
)
SOURCE_NOTICE_CONTENT = (
    b"// Synthetic upstream fixture\r\n"
    b"// SPDX-FileCopyrightText: 2009 Florian Loitsch\r\n"
    b"// SPDX-License-Identifier: MIT\r\n"
    b"void upstream_function();\r\n"
)


def add_legal_payload(package_root: Path) -> None:
    legal = package_root / "share/opk/licenses"
    document = REPO_ROOT / "docs/third-party-licenses.md"
    for relative in (*release_tool.OPK_LEGAL_NOTICES, *release_tool.CORE_LEGAL_NOTICES,
                     "executorch/LICENSE", "numpy-headers/licenses/LICENSE.txt",
                     "python-numpy/licenses/LICENSE.txt", "python-flatbuffers/LICENSE"):
        path = legal / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f"Original {relative} terms\n")
    for name, relative in SOURCE_NOTICES:
        path = legal / name / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(SOURCE_NOTICE_CONTENT)
    shutil.copyfile(document, legal / document.name)


def add_model(repo_root: Path, model_id: str, filename: str, op_id: str) -> None:
    model_root = repo_root / "config/models" / model_id
    model_root.mkdir(parents=True, exist_ok=True)
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
    archive = root / f"open-perception-kit-{version}.zip"
    archive.write_bytes(b"sdk")
    (root / f"{archive.name}.sha256").write_text("checksum\n", encoding="utf-8")
    (root / f"{archive.name}.provenance.json").write_text(
        json.dumps({"dirty": dirty, "repository_commit": commit}), encoding="utf-8"
    )
    return archive


def add_release_identity(repo_root: Path) -> None:
    development_root = repo_root / "development"
    development_root.mkdir(parents=True, exist_ok=True)
    (development_root / "meson.build").write_text(
        "project('demo', version: '0.1.0')\n", encoding="utf-8"
    )
    python = repo_root / "generated/open_perception_kit/python/pyproject.toml"
    cargo = repo_root / "generated/open_perception_kit/rust/Cargo.toml"
    python.parent.mkdir(parents=True, exist_ok=True)
    cargo.parent.mkdir(parents=True, exist_ok=True)
    python.write_text('[project]\nversion = "0.1.0"\n', encoding="utf-8")
    cargo.write_text('[package]\nversion = "0.1.0"\n', encoding="utf-8")


class ReleaseToolTests(unittest.TestCase):
    def run_tool(self, *arguments: str, cwd: Path = REPO_ROOT) -> subprocess.CompletedProcess[str]:
        environment = os.environ.copy()
        environment.pop("GITHUB_OUTPUT", None)
        return subprocess.run(
            ["python3", str(TOOL), *arguments],
            cwd=cwd,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

    def test_package_versions_match_the_product_version(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            add_release_identity(root)
            self.assertEqual(
                release_tool.read_package_versions(root, "0.1.0"),
                ("0.1.0", "0.1.0"),
            )

            python = root / "generated/open_perception_kit/python/pyproject.toml"
            cargo = root / "generated/open_perception_kit/rust/Cargo.toml"
            python.write_text('[project]\nversion = "0.1.0.dev0"\n', encoding="utf-8")
            cargo.write_text('[package]\nversion = "0.1.0-dev.0"\n', encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "do not match"):
                release_tool.read_package_versions(root, "0.1.0")

            python.write_text('[project]\nversion = "0.1.0"\n', encoding="utf-8")
            cargo.write_text('[package]\nversion = "0.2.0"\n', encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "do not match"):
                release_tool.read_package_versions(root, "0.1.0")

    def test_release_tree_rejects_redistributed_python_packages(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for relative in (
                "lib/opk/opk-python-ops.so",
                "share/opk/models/example/postprocess.py",
                "share/opk/python/opk_python_ops.pyi",
                "share/opk/python/pyproject.toml",
                "share/opk/python/requirements.txt",
                "share/opk/open-perception-kit/open-perception-kit-0.1.0.zip",
            ):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            release_tool.validate_release_tree(root)

            package = root / "share/opk/python/numpy/__init__.py"
            package.parent.mkdir(parents=True)
            package.touch()
            with self.assertRaisesRegex(RuntimeError, "Redistributed Python package"):
                release_tool.validate_release_tree(root)

    def test_onnxruntime_provenance_requires_locked_source_and_notices(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            package_root = root / "package"
            library = package_root / "lib/opk/libonnxruntime.so.1.24.4"
            library.parent.mkdir(parents=True)
            library.write_bytes(b"onnxruntime")
            legal_root = package_root / "share/opk/licenses/onnxruntime"
            legal_root.mkdir(parents=True)
            (legal_root / "LICENSE").write_text("license", encoding="utf-8")
            notice = legal_root / "ThirdPartyNotices.txt"
            notice.write_text("notices", encoding="utf-8")
            repo_root = root / "source"
            descriptor = repo_root / "requirements/build.json"
            descriptor.parent.mkdir(parents=True)
            descriptor.write_text(json.dumps({
                "onnxruntime": "1.24.4",
                "onnxruntime-sha256-x64": "a" * 64,
            }), encoding="utf-8")
            import hashlib
            receipt = release_tool.onnxruntime_archive_source(descriptor, "x86_64")
            receipt["library_sha256"] = hashlib.sha256(library.read_bytes()).hexdigest()
            library.with_name(f"{library.name}.provenance.json").write_text(
                json.dumps(receipt), encoding="utf-8"
            )
            release_tool.validate_onnxruntime_provenance(package_root, repo_root, "x86_64")
            (library.parent / "libonnxruntime.so.1").symlink_to(library.name)
            with patch.object(release_tool, "dynamic_values", return_value=["libonnxruntime.so.1"]):
                release_tool.validate_onnx_runtime(library.parent)
            notice.unlink()
            with self.assertRaisesRegex(RuntimeError, "legal file"):
                release_tool.validate_onnxruntime_provenance(package_root, repo_root, "x86_64")
            notice.write_text("notices", encoding="utf-8")
            library.write_bytes(b"changed")
            with self.assertRaisesRegex(RuntimeError, "library SHA-256 mismatch"):
                release_tool.validate_onnxruntime_provenance(package_root, repo_root, "x86_64")

    def test_elf_dependencies_can_resolve_through_rpath(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            package_root = Path(temporary)
            library_root = package_root / "share/opk/testlibs"
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

    def test_stages_descriptors_without_local_model_binaries(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / OPCHAINS_DIR).mkdir(parents=True)
            add_release_models(root)
            model_root = root / "config/models/nitec-resnet-18"
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
                (stage_root / "share/opk/models/nitec-resnet-18/model.json").read_text(
                    encoding="utf-8"
                )
            )
            opchain = json.loads(
                (stage_root / "share/opk/models/nitec-resnet-18/opchain.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(descriptor["modelFile"], ONNX_MODEL_FILE)
            self.assertEqual(
                opchain["ops"][0]["attributes"]["modelDescriptor"], MODEL_DESCRIPTOR
            )
            self.assertTrue(
                (stage_root / "share/opk/models/nitec-resnet-18/secondary.json").is_file()
            )
            self.assertFalse(list(stage_root.rglob("*.onnx")))
            self.assertFalse(list(stage_root.rglob("*.pte")))

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
                    for path in (stage_root / "share/opk/models").iterdir()
                },
                release_tool.RELEASE_MODEL_NAMES,
            )

    def test_rejects_wrong_executorch_backend_or_suffix(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / OPCHAINS_DIR).mkdir(parents=True)
            add_release_models(root)
            model_root = root / "config/models/nitec-resnet-18-executorch"
            opchain_path = model_root / "opchain.json"
            opchain = json.loads(opchain_path.read_text(encoding="utf-8"))
            opchain["ops"][0]["id"] = ONNX_INFERENCE_OP
            opchain_path.write_text(json.dumps(opchain), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, EXECUTORCH_INFERENCE_OP):
                release_tool.discover_models(root)

            opchain["ops"][0]["id"] = EXECUTORCH_INFERENCE_OP
            opchain_path.write_text(json.dumps(opchain), encoding="utf-8")
            descriptor = model_root / "model.json"
            descriptor.write_text(
                json.dumps({"modelFile": "model.onnx"}), encoding="utf-8"
            )
            with self.assertRaisesRegex(RuntimeError, "unsupported model file"):
                release_tool.discover_models(root)

    def test_rejects_wrong_release_model_backend_or_suffix(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / OPCHAINS_DIR).mkdir(parents=True)
            add_release_models(root)
            yolo26n_opchain = root / "config/models/yolo26n-320/opchain.json"
            opchain = json.loads(yolo26n_opchain.read_text(encoding="utf-8"))
            opchain["ops"][0]["id"] = EXECUTORCH_INFERENCE_OP
            yolo26n_opchain.write_text(json.dumps(opchain), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, ONNX_INFERENCE_OP):
                release_tool.discover_models(root)

            opchain["ops"][0]["id"] = ONNX_INFERENCE_OP
            yolo26n_opchain.write_text(json.dumps(opchain), encoding="utf-8")
            descriptor = root / "config/models/yolo26n-320/model.json"
            descriptor.write_text(json.dumps({"modelFile": "model.pte"}), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "unsupported model file"):
                release_tool.discover_models(root)

    def test_rejects_retired_release_content(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            package_root = Path(temporary)
            for relative in (
                "lib/opk/libhailort.so",
                "share/opk/models/retired/model.hef",
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

    def test_rejects_bundled_models_in_any_release_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for relative in (
                "share/opk/models/example/model.onnx",
                "lib/opk/model.pte",
                "work/config/models/example/renamed.bin",
                "work/config/models/example/renamed.bin.part",
                "share/opk/licenses/model.ONNX",
            ):
                with self.subTest(relative=relative):
                    path = root / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.touch()
                    with self.assertRaisesRegex(RuntimeError, "Forbidden model binary"):
                        release_tool.validate_release_tree(root)
                    path.unlink()

    def test_allows_source_named_legal_documentation_directories(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            package_root = Path(temporary)
            plugin_root = package_root / PLUGIN_DIR
            plugin_root.mkdir(parents=True)
            for plugin_name in release_tool.PLUGIN_NAMES:
                (plugin_root / plugin_name).touch()
            legal_root = package_root / "share/opk/licenses/libexecutorch-dev/examples"
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
            model_root = package_root / "share/opk/models"
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
            private_root = package_root / "lib/opk"
            model_root = package_root / "share/opk/models"
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

    def test_requires_regular_python_stub(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in release_tool.PLUGIN_NAMES:
                path = root / PLUGIN_DIR / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"\x7fELF")
            for name in release_tool.OP_MODULE_NAMES | {release_tool.RUNTIME_LIBRARY_NAME, "libopk-common.so"}:
                path = root / "lib/opk" / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"\x7fELF")
            for name in release_tool.RELEASE_MODEL_NAMES:
                (root / "share/opk/models" / name).mkdir(parents=True)
            stub = root / "share/opk/python/opk_python_ops.pyi"
            stub.parent.mkdir()
            stub.write_text("# PythonScript types\n")
            release_tool.validate_runtime_files(root)

            stub.unlink()
            with self.assertRaisesRegex(RuntimeError, "PythonScript type stub"):
                release_tool.validate_runtime_files(root)
            stub.mkdir()
            with self.assertRaisesRegex(RuntimeError, "PythonScript type stub"):
                release_tool.validate_runtime_files(root)
            stub.rmdir()
            stub.symlink_to(root / "lib/opk/libopk-common.so")
            with self.assertRaisesRegex(RuntimeError, "PythonScript type stub"):
                release_tool.validate_runtime_files(root)

    def test_requires_complete_original_legal_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            package_root = Path(temporary)
            legal_root = package_root / "share/opk/licenses"
            add_legal_payload(package_root)
            release_tool.validate_legal_documentation(package_root, require_python=True)
            for relative in (*release_tool.CORE_LEGAL_NOTICES, "executorch/LICENSE",
                             "python-numpy/licenses/LICENSE.txt", "python-flatbuffers/LICENSE"):
                path = legal_root / relative
                original = path.read_bytes()
                for content in (None, b""):
                    with self.subTest(notice=relative, missing_or_empty=content):
                        if content is None:
                            path.unlink()
                        else:
                            path.write_bytes(content)
                        with self.assertRaisesRegex(RuntimeError, "licence evidence is (missing|empty)"):
                            release_tool.validate_legal_documentation(package_root, require_python=True)
                path.write_bytes(original)

    def test_release_legal_validation_does_not_require_host_python_notices(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            add_legal_payload(root)
            legal = root / "share/opk/licenses"
            for name in ("python-numpy", "python-flatbuffers"):
                shutil.rmtree(legal / name)
            release_tool.validate_legal_documentation(root)

    def test_python_module_requires_numpy_header_notice(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            add_legal_payload(root)
            plugin = root / "lib/opk/opk-python-ops.so"
            plugin.parent.mkdir(parents=True)
            plugin.touch()
            release_tool.validate_legal_documentation(root, require_backends=False)

            legal = root / "share/opk/licenses"
            shutil.rmtree(legal / "numpy-headers")
            with self.assertRaisesRegex(RuntimeError, "numpy-headers"):
                release_tool.validate_legal_documentation(root, require_backends=False)

    def test_requires_notices_for_installed_image_runtimes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            add_legal_payload(root)
            legal = root / "share/opk/licenses"
            for name in ("python-numpy", "python-flatbuffers", "executorch"):
                shutil.rmtree(legal / name)
            # Cairn ships neither the OPK Python environment nor ExecuTorch.
            release_tool.validate_legal_documentation(root, require_backends=False)
            with self.assertRaisesRegex(RuntimeError, "python-flatbuffers.*python-numpy"):
                release_tool.validate_legal_documentation(
                    root, require_backends=False, require_python=True
                )
            with self.assertRaisesRegex(RuntimeError, "executorch"):
                release_tool.validate_legal_documentation(root, require_backends=True)
            (legal / "executorch").mkdir()
            (legal / "executorch/GIT_COMMIT_ID").write_text("a" * 40 + "\n")
            with self.assertRaisesRegex(RuntimeError, "executorch"):
                release_tool.validate_legal_documentation(root, require_backends=False)

    def test_stage_legal_uses_markdown_and_copies_it_unchanged(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo = root / "source"
            for relative in ("LICENSE", "NOTICE", "docs/third-party-licenses.md", "docs/public/licensing.md"):
                target = repo / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(REPO_ROOT / relative, target)
            document = repo / "docs/third-party-licenses.md"
            for name in release_tool.MESON_COMPONENTS:
                subprojects = repo / "development/subprojects"
                source = subprojects / f"{name}-test"
                source.mkdir(parents=True)
                (subprojects / f"{name}.wrap").write_text(f"[wrap-file]\ndirectory = {source.name}\n")
                for notice in release_tool.CORE_LEGAL_NOTICES:
                    if notice.startswith(f"{name}/"):
                        (source / Path(notice).name).write_bytes(b"Synthetic upstream owner\r\nOriginal terms\r\n")
                for component, relative in SOURCE_NOTICES:
                    if component == name:
                        embedded_notice = source / relative
                        embedded_notice.parent.mkdir(parents=True, exist_ok=True)
                        embedded_notice.write_bytes(SOURCE_NOTICE_CONTENT)
            for name, filename in (("flatbuffers", "LICENSE"), ("fontawesome", "LICENSE.txt")):
                source = repo / "development/web/content/vendor" / name
                source.mkdir(parents=True)
                shutil.copyfile(REPO_ROOT / "development/web/content/vendor" / name / filename, source / filename)
            onnx = root / "deps/onnxruntime/share/doc/onnxruntime"
            onnx.mkdir(parents=True)
            for filename in ("LICENSE", "ThirdPartyNotices.txt"):
                (onnx / filename).write_text("Synthetic ONNX Runtime evidence\n")
            args = SimpleNamespace(repo_root=repo, deps_root=root / "deps",
                                   stage_root=root / "stage", include_python=False)
            plugin = args.stage_root / "lib/opk/opk-python-ops.so"
            plugin.parent.mkdir(parents=True)
            plugin.touch()
            wheel_metadata = root / "installed/numpy-2.4.2.dist-info"
            (wheel_metadata / "licenses").mkdir(parents=True)
            (wheel_metadata / "licenses/LICENSE.txt").write_text("Synthetic installed NumPy terms\n")
            distribution = SimpleNamespace(
                files=[Path("numpy-2.4.2.dist-info/METADATA")],
                locate_file=lambda _: wheel_metadata / "METADATA",
            )
            with patch.object(release_tool.importlib.metadata, "distribution", return_value=distribution):
                release_tool.stage_legal(args)
            legal = args.stage_root / "share/opk/licenses"
            release_tool.validate_legal_documentation(args.stage_root, require_backends=False)
            self.assertEqual((legal / "asio/COPYING").read_bytes(), b"Synthetic upstream owner\r\nOriginal terms\r\n")
            report = legal / "third-party-licenses.md"
            self.assertEqual(report.read_bytes(), document.read_bytes())
            self.assertFalse((legal / "THIRD_PARTY_LICENSES.md").exists())
            self.assertFalse((legal / "components.json").exists())
            # Original source evidence belongs only in the legal-documentation tree.
            release_tool.validate_release_tree(args.stage_root)
            self.assertFalse((legal / "python-numpy").exists())
            self.assertFalse((legal / "python-flatbuffers").exists())
            self.assertEqual(
                (legal / "numpy-headers/licenses/LICENSE.txt").read_bytes(),
                (wheel_metadata / "licenses/LICENSE.txt").read_bytes(),
            )
            for name, relative in SOURCE_NOTICES:
                notice = f"{name}/{relative}"
                self.assertEqual((legal / notice).read_bytes(), SOURCE_NOTICE_CONTENT)
                source_notice = repo / "development/subprojects" / f"{name}-test" / relative
                for content in (None, b""):
                    with self.subTest(component=name, missing_or_empty=content):
                        if content is None:
                            source_notice.unlink()
                        else:
                            source_notice.write_bytes(content)
                        args.stage_root = root / f"invalid-{name}-{Path(relative).name}-{content is None}"
                        with self.assertRaisesRegex(RuntimeError, "licence evidence"):
                            release_tool.stage_legal(args)
                source_notice.write_bytes(SOURCE_NOTICE_CONTENT)
            args.stage_root = root / "stage"
            report.unlink()
            with self.assertRaisesRegex(RuntimeError, "licence evidence is missing"):
                release_tool.validate_legal_documentation(args.stage_root, require_backends=False)

            executorch = root / "deps/executorch-legal-documentation"
            executorch.mkdir()
            (executorch / "LICENSE").write_text("Synthetic ExecuTorch evidence\n")
            (executorch / "GIT_COMMIT_ID").write_text("a" * 40 + "\n")
            args.stage_root = root / "with-executorch"
            release_tool.stage_legal(args)
            self.assertEqual(
                (args.stage_root / "share/opk/licenses/executorch/GIT_COMMIT_ID").read_text(), "a" * 40 + "\n")
            (executorch / "GIT_COMMIT_ID").unlink()
            args.stage_root = root / "without-executorch-provenance"
            release_tool.stage_legal(args)
            self.assertFalse((args.stage_root / "share/opk/licenses/executorch/GIT_COMMIT_ID").exists())

            flatbuffers_metadata = root / "installed/flatbuffers-25.9.23.dist-info"
            flatbuffers_metadata.mkdir()
            (flatbuffers_metadata / "LICENSE").write_text("Synthetic installed FlatBuffers terms\n")
            distributions = {
                "numpy": distribution,
                "flatbuffers": SimpleNamespace(
                    files=[Path("flatbuffers-25.9.23.dist-info/METADATA")],
                    locate_file=lambda _: flatbuffers_metadata / "METADATA",
                ),
            }
            args.stage_root = root / "with-python"
            args.include_python = True
            with patch.object(release_tool.importlib.metadata, "distribution", side_effect=distributions.__getitem__):
                release_tool.stage_legal(args)
            release_tool.validate_legal_documentation(args.stage_root, require_python=True)
            self.assertFalse((args.stage_root / "share/opk/licenses/components.json").exists())

    def test_legal_validation_rejects_symlinks_and_empty_nested_notices(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            legal = root / "share/opk/licenses"
            add_legal_payload(root)
            outside = root / "outside"
            outside.mkdir()
            (outside / "LICENSE").write_text("External terms\n")
            for target in (outside, outside / "LICENSE"):
                with self.subTest(target=target):
                    link = legal / "asio/third-party"
                    link.symlink_to(target)
                    with self.assertRaisesRegex(RuntimeError, "non-regular entry"):
                        release_tool.validate_legal_documentation(root)
                    link.unlink()
            nested = legal / "asio/third-party/NOTICE"
            nested.parent.mkdir()
            nested.touch()
            with self.assertRaisesRegex(RuntimeError, "licence evidence is empty"):
                release_tool.validate_legal_documentation(root)

    def test_requires_embedded_upstream_notices(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name, relative in SOURCE_NOTICES:
                for content in (None, b""):
                    with self.subTest(component=name, missing_or_empty=content):
                        add_legal_payload(root)
                        legal = root / "share/opk/licenses"
                        notice = f"{name}/{relative}"
                        if content is None:
                            (legal / notice).unlink()
                        else:
                            (legal / notice).write_bytes(content)
                        with self.assertRaisesRegex(RuntimeError, "licence evidence"):
                            release_tool.validate_legal_documentation(root, require_backends=False)

    def test_legal_validation_requires_artifact_notices(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            legal = root / "share/opk/licenses"
            for name in ("python-numpy", "python-flatbuffers"):
                with self.subTest(component=name):
                    add_legal_payload(root)
                    shutil.rmtree(legal / name)
                    result = self.run_tool("validate-legal", "--package-root", str(root), "--require-python")
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("licence evidence is missing", result.stderr)
                    # Cairn does not contain the OPK Python runtime or ExecuTorch.
                    release_tool.validate_legal_documentation(root, require_backends=False)
            for notice in ("LICENSE", "NOTICE", "third-party-licenses.md", "README.md"):
                with self.subTest(notice=notice):
                    add_legal_payload(root)
                    (legal / notice).unlink()
                    with self.assertRaisesRegex(RuntimeError, "licence evidence is missing"):
                        release_tool.validate_legal_documentation(root, require_backends=False)

    def test_legal_validation_accepts_demo_media(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            add_legal_payload(root)
            media = root / "work/data/images/GettyImages-1140581459-thumbnail.jpg"
            media.parent.mkdir(parents=True)
            media.write_bytes(b"image")
            release_tool.validate_legal_documentation(root, require_backends=False)
            release_tool.validate_legal_documentation(root, require_backends=False, require_python=True)

    def test_notice_collection_preserves_upstream_bytes_and_nested_notices(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            (source / "third-party/dependency").mkdir(parents=True)
            (source / "LICENSE").write_bytes(b"Upstream owner\r\nOriginal terms\r\n")
            (source / "third-party/dependency/NOTICE").write_text("Another owner\n")
            (source / "source.cpp").write_text("code")
            destination = root / "notices"
            release_tool.copy_legal_files(source, destination)
            self.assertEqual(
                release_tool.payload_files(destination),
                {Path("LICENSE"), Path("third-party/dependency/NOTICE")},
            )
            for path in ("LICENSE", "third-party/dependency/NOTICE"):
                self.assertEqual((source / path).read_bytes(), (destination / path).read_bytes())
            (source / "LICENSE").write_bytes(b"")
            with self.assertRaisesRegex(RuntimeError, "empty licence"):
                release_tool.copy_legal_files(source, root / "invalid")

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
                                        "../../models/yolo26n-320/model.json"
                                    )
                                },
                            },
                            {
                                "id": ONNX_INFERENCE_OP,
                                "attributes": {
                                    "modelDescriptor": (
                                        "/work/config/models/ultraface-rfb-320/model.json"
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
                    package_root / "share/opk/opchains/tracking/demo.json"
                ).read_text(encoding="utf-8")
            )
            self.assertEqual(
                [
                    op["attributes"]["modelDescriptor"]
                    for op in staged_opchain["ops"]
                ],
                [
                    "../../models/yolo26n-320/model.json",
                    "../../models/ultraface-rfb-320/model.json",
                ],
            )
            release_tool.validate_release_payload(package_root, repo_root)

            model_path = package_root / "share/opk/models/yolo26n-320" / MODEL_DESCRIPTOR
            model = model_path.read_bytes()
            model_path.unlink()
            with self.assertRaisesRegex(RuntimeError, "models payload"):
                release_tool.validate_release_payload(package_root, repo_root)
            model_path.write_bytes(model)

            unexpected_model = model_path.with_name("unexpected.bin")
            unexpected_model.write_bytes(b"model")
            with self.assertRaisesRegex(RuntimeError, "only configuration files"):
                release_tool.validate_release_payload(package_root, None)
            unexpected_model.unlink()

            opchain_path = package_root / "share/opk/opchains/tracking/demo.json"
            opchain = opchain_path.read_bytes()
            opchain_path.unlink()
            with self.assertRaisesRegex(RuntimeError, "opchains payload"):
                release_tool.validate_release_payload(package_root, repo_root)
            opchain_path.write_bytes(opchain)

            model_opchain = package_root / "share/opk/models/yolo26n-320/opchain.json"
            model_opchain.write_text(
                json.dumps(
                    {"modelDescriptor": "/work/config/models/yolo26n-320/model.json"}
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
                package_root / "share/opk/schemas/json/v1/opchain/common.schema.json"
            )
            self.assertEqual(packaged_schema.read_bytes(), nested_schema.read_bytes())
            self.assertFalse(
                (package_root / "share/opk/schemas/json/perception.schema.json").exists()
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

    def test_preparation_requires_matching_changelog(self) -> None:
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

            final = self.run_tool(*arguments)
            self.assertNotEqual(final.returncode, 0)
            self.assertIn("no non-empty 0.1.0 section", final.stderr)

            (root / "CHANGELOG.md").write_text(
                "# Changelog\n\n## [0.1.0]\n\nInitial release\n", encoding="utf-8"
            )
            final = self.run_tool(*arguments)
            self.assertEqual(final.returncode, 0, final.stderr)
            self.assertIn("version=0.1.0", final.stdout.splitlines())

    def test_model_path_cannot_escape_its_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            model_root = root / "config/models/mobilegaze-mobilenet-v2"
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
