#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

"""Build-time checks and staging for OPK release packages."""

from __future__ import annotations

import argparse
import configparser
import filecmp
import hashlib
import importlib.metadata
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from urllib.parse import quote

ARCHITECTURES = {"x86_64", "aarch64"}
MESON_COMPONENTS = {
    "asio", "cpp-httplib", "fmt", "gtest", "jsoncons", "magic_enum",
    "nlohmann_json", "stb", "tl-expected", "websocketpp",
}
LEGAL_PREFIXES = ("license", "licence", "copying", "notice", "copyright", "thirdpartynotices", "git_commit_id")
# These source files carry upstream attributions absent from the top-level licences.
# Preserve their complete original bytes in the legal tree, including embedded terms.
SOURCE_LEGAL_NOTICES = (
    ("jsoncons", "include/jsoncons/detail/grisu3.hpp"),
    ("nlohmann_json", "include/nlohmann/detail/conversions/to_chars.hpp"),
    ("nlohmann_json", "include/nlohmann/thirdparty/hedley/hedley.hpp"),
)
CORE_LEGAL_COMPONENTS = MESON_COMPONENTS | {"opk", "onnxruntime", "flatbuffers", "fontawesome"}
OPK_LEGAL_NOTICES = ("LICENSE", "NOTICE", "THIRD_PARTY_NOTICE.md", "README.md")
ONNX_INFERENCE_OP = "opk-onnx-ops/Inference"
ONNX_MODEL_SUFFIX = ".onnx"
EXECUTORCH_INFERENCE_OP = "opk-executorch-ops/Inference"
EXECUTORCH_MODEL_SUFFIX = ".pte"
RELEASE_MODELS = {
    "mobilegaze-mobilenet-v2": (ONNX_INFERENCE_OP, ONNX_MODEL_SUFFIX),
    "mobilegaze-mobilenet-v2-executorch": (
        EXECUTORCH_INFERENCE_OP,
        EXECUTORCH_MODEL_SUFFIX,
    ),
    "nitec-resnet-18": (ONNX_INFERENCE_OP, ONNX_MODEL_SUFFIX),
    "nitec-resnet-18-executorch": (
        EXECUTORCH_INFERENCE_OP,
        EXECUTORCH_MODEL_SUFFIX,
    ),
    "osnet-x0-25": (ONNX_INFERENCE_OP, ONNX_MODEL_SUFFIX),
    "ultraface-rfb-320": (ONNX_INFERENCE_OP, ONNX_MODEL_SUFFIX),
    "yolo26n-320": (ONNX_INFERENCE_OP, ONNX_MODEL_SUFFIX),
    "yolo26n-480": (ONNX_INFERENCE_OP, ONNX_MODEL_SUFFIX),
    "yolo26n-640": (ONNX_INFERENCE_OP, ONNX_MODEL_SUFFIX),
    "yolo26s-320": (ONNX_INFERENCE_OP, ONNX_MODEL_SUFFIX),
    "yolo26s-480": (ONNX_INFERENCE_OP, ONNX_MODEL_SUFFIX),
    "yolo26s-640": (ONNX_INFERENCE_OP, ONNX_MODEL_SUFFIX),
}
RELEASE_MODEL_NAMES = set(RELEASE_MODELS)
PLUGIN_NAMES = {
    "libopkcomm.so",
    "libopkinfer.so",
    "libopkosd.so",
    "libopkperformance.so",
    "libopksink.so",
    "libopktracker.so",
}
OP_MODULE_NAMES = {
    "opk-executorch-ops.so",
    "opk-onnx-ops.so",
    "opk-python-ops.so",
    "opk-std-ops.so",
}
RUNTIME_LIBRARY_NAME = "opk-runtime.so"
# Temporary EXPKITS-1084 quality gate for retired release payloads.
RETIRED_RELEASE_PATH_MARKERS = ("hailo",)
RETIRED_RELEASE_SUFFIXES = {".hef"}
SYSTEM_LIBRARY_PREFIXES = (
    "ld-linux-",
    "libblkid.so.",
    "libbrotlicommon.so.",
    "libbrotlidec.so.",
    "libbrotlienc.so.",
    "libc.so.",
    "libcap.so.",
    "libcrypto.so.",
    "libdl.so.",
    "libdw.so.",
    "libelf.so.",
    "libffi.so.",
    "libgcc_s.so.",
    "libgio-2.0.so.",
    "libglib-2.0.so.",
    "libgmodule-2.0.so.",
    "libgobject-2.0.so.",
    "libgst",
    "libjson-glib-1.0.so.",
    "libm.so.",
    "libmount.so.",
    "libnice.so.",
    "liborc-0.4.so.",
    "libpcre2-8.so.",
    "libpthread.so.",
    "libpython3.13.so.",
    "libresolv.so.",
    "librt.so.",
    "libselinux.so.",
    "libsoup-3.0.so.",
    "libssl.so.",
    "libstdc++.so.",
    "libunwind.so.",
    "libusb-1.0.so.",
    "libz.so.",
    "libzstd.so.",
)
VERSION_PATTERN = re.compile(r"^\d+\.\d+\.\d+$", re.ASCII)
JSON_GLOB = "*.json"
REPO_ROOT = Path(__file__).resolve().parents[2]

GIT_COMMIT_PATTERN = re.compile(r"^[0-9a-f]{40}$", re.ASCII)
PERCEPTION_SDK_ARCHIVE_PATTERN = re.compile(
    r"^open-perception-kit-(\d+\.\d+\.\d+)\.zip$", re.ASCII
)


def fail(message: str) -> None:
    raise RuntimeError(message)


def load_json(path: Path) -> object:
    with path.open(encoding="utf-8") as stream:
        return json.load(stream)


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")


def check_safe_relative(value: str, field: str) -> Path:
    path = Path(value)
    if not value or path.is_absolute() or ".." in path.parts:
        fail(f"{field} must be a safe relative path: {value!r}")
    return path


def resolve_model_path(model_root: Path, value: object, field: str) -> Path:
    if not isinstance(value, str):
        fail(f"{field} must be a path")
    path = Path(value)
    if path.is_absolute():
        try:
            path = path.relative_to(Path("/work/config/models") / model_root.name)
        except ValueError:
            fail(f"{field} is outside its model directory: {value!r}")
    path = check_safe_relative(str(path), field)
    resolved = (model_root / path).resolve()
    try:
        resolved.relative_to(model_root.resolve())
    except ValueError:
        fail(f"{field} resolves outside its model directory: {value!r}")
    return resolved


def find_primary_descriptor(model_root: Path, model_id: str) -> Path:
    opchain_path = model_root / "opchain.json"
    if not opchain_path.is_file():
        fail(f"{model_id}: opchain.json does not exist")
    opchain = load_json(opchain_path)
    if not isinstance(opchain, dict) or not isinstance(opchain.get("ops"), list):
        fail(f"{model_id}: invalid model opchain")

    expected_backend = RELEASE_MODELS[model_id][0]
    inference_ops = []
    for op in opchain["ops"]:
        if not isinstance(op, dict):
            fail(f"{model_id}: invalid op entry")
        if op.get("id") == expected_backend:
            inference_ops.append(op)
    if len(inference_ops) != 1:
        fail(
            f"{model_id}: model opchain must contain exactly one {expected_backend} op"
        )
    attributes = inference_ops[0].get("attributes")
    if not isinstance(attributes, dict) or "modelDescriptor" not in attributes:
        fail(f"{model_id}: inference op has no modelDescriptor")
    return resolve_model_path(
        model_root, attributes["modelDescriptor"], f"{model_id}.modelDescriptor"
    )


def collect_model_files(
    model_root: Path, model_id: str, primary_descriptor: Path
) -> list[Path]:
    config_paths = sorted(model_root.rglob(JSON_GLOB))
    model_suffix = RELEASE_MODELS[model_id][1]
    for config_path in config_paths:
        config = load_json(config_path)
        if not isinstance(config, dict):
            fail(f"{model_id}: invalid model config: {config_path.name}")
        if "modelFile" not in config:
            if config_path == primary_descriptor:
                fail(f"{model_id}: modelDescriptor is not a model config")
            continue
        model_path = resolve_model_path(
            model_root, config["modelFile"], f"{model_id}.{config_path.name}.modelFile"
        )
        if model_path.suffix != model_suffix:
            fail(f"{model_id}: unsupported model file: {model_path.name}")
    if primary_descriptor not in config_paths:
        fail(f"{model_id}: modelDescriptor is not a model config")
    return config_paths


def discover_models(repo_root: Path) -> dict[str, dict[str, object]]:
    models_root = repo_root / "config/models"
    if not models_root.is_dir():
        fail(f"Model directory does not exist: {models_root}")
    models: dict[str, dict[str, object]] = {}
    for model_id in sorted(RELEASE_MODEL_NAMES):
        model_root = models_root / model_id
        if not model_root.is_dir():
            fail(f"Release model directory does not exist: {model_root}")
        config_paths = collect_model_files(
            model_root, model_id, find_primary_descriptor(model_root, model_id)
        )
        models[model_id] = {
            "config_paths": config_paths,
            "root": model_root,
        }
    return models


def rewrite_model_opchain(opchain: object, model_id: str, source_root: Path) -> object:
    if not isinstance(opchain, dict) or not isinstance(opchain.get("ops"), list):
        fail(f"{model_id}: invalid model opchain")
    for op in opchain["ops"]:
        if not isinstance(op, dict):
            fail(f"{model_id}: invalid op entry")
        attributes = op.get("attributes")
        if isinstance(attributes, dict) and "modelDescriptor" in attributes:
            descriptor_path = resolve_model_path(
                source_root,
                attributes["modelDescriptor"],
                f"{model_id}.modelDescriptor",
            )
            attributes["modelDescriptor"] = str(descriptor_path.relative_to(source_root))
    return opchain


def resolve_shared_model_descriptor(
    value: object, source_path: Path, models_root: Path
) -> tuple[str, str] | None:
    if not isinstance(value, str):
        return None
    descriptor_path = Path(value)
    if descriptor_path.is_absolute():
        try:
            descriptor_path = models_root / descriptor_path.relative_to(
                "/work/config/models"
            )
        except ValueError:
            return None
    else:
        descriptor_path = source_path.parent / descriptor_path
    try:
        model_id, descriptor_name = descriptor_path.resolve().relative_to(
            models_root.resolve()
        ).parts
    except ValueError:
        return None
    return model_id, descriptor_name


def rewrite_shared_opchain(
    opchain: object,
    source_path: Path,
    destination_path: Path,
    models_root: Path,
    selected: dict[str, dict[str, object]],
    stage_root: Path,
) -> object | None:
    if not isinstance(opchain, dict) or not isinstance(opchain.get("ops"), list):
        return None
    for op in opchain["ops"]:
        if not isinstance(op, dict):
            return None
        attributes = op.get("attributes")
        if not isinstance(attributes, dict) or "modelDescriptor" not in attributes:
            continue
        resolved = resolve_shared_model_descriptor(
            attributes["modelDescriptor"], source_path, models_root
        )
        if resolved is None:
            return None
        model_id, descriptor_name = resolved
        descriptor_path = models_root / model_id / descriptor_name
        if (
            model_id not in selected
            or descriptor_path not in selected[model_id]["config_paths"]
        ):
            return None
        target_descriptor = stage_root / "share/opk/models" / model_id / descriptor_name
        attributes["modelDescriptor"] = os.path.relpath(
            target_descriptor, destination_path.parent
        )
    return opchain


def stage_models(args: argparse.Namespace) -> None:
    repo_root = Path(args.repo_root).resolve()
    stage_root = Path(args.stage_root).resolve()
    selected = discover_models(repo_root)

    for model_id, entry in selected.items():
        source_root = entry["root"]
        model_root = stage_root / "share/opk/models" / model_id
        model_root.mkdir(parents=True, exist_ok=False)
        for source_path in entry["config_paths"]:
            destination_path = model_root / source_path.relative_to(source_root)
            config = load_json(source_path)
            if source_path.name == "opchain.json":
                config = rewrite_model_opchain(config, model_id, source_root)
            elif "modelFile" in config:
                source_model = resolve_model_path(
                    source_root,
                    config["modelFile"],
                    f"{model_id}.{source_path.name}.modelFile",
                )
                config["modelFile"] = os.path.relpath(
                    model_root / source_model.relative_to(source_root),
                    destination_path.parent,
                )
            write_json(destination_path, config)

    source_root = repo_root / "config/opchains"
    target_root = stage_root / "share/opk/opchains"
    for source_path in source_root.rglob(JSON_GLOB):
        destination_path = target_root / source_path.relative_to(source_root)
        rewritten = rewrite_shared_opchain(
            load_json(source_path),
            source_path,
            destination_path,
            repo_root / "config/models",
            selected,
            stage_root,
        )
        if rewritten is not None:
            write_json(destination_path, rewritten)

    source_root = repo_root / "config/schemas/v1"
    validate_schema_tree(source_root)
    shutil.copytree(source_root, stage_root / "share/opk/schemas/json/v1")

    for json_path in (stage_root / "share/opk").rglob(JSON_GLOB):
        content = json_path.read_text(encoding="utf-8")
        if "/work/" in content:
            fail(f"Build-machine path remains in {json_path}")


def json_mapping(path: Path) -> dict[str, object]:
    value = load_json(path)
    if not isinstance(value, dict):
        fail(f"Expected a JSON object in {path}")
    return value


def payload_files(root: Path) -> set[Path]:
    if root.is_symlink() or (root.exists() and not root.is_dir()):
        fail(f"Release payload directory is invalid: {root}")
    if not root.exists():
        return set()
    files: set[Path] = set()
    for path in root.rglob("*"):
        if path.is_symlink() or (not path.is_file() and not path.is_dir()):
            fail(f"Release payload contains a non-regular entry: {path}")
        if path.is_file():
            files.add(path.relative_to(root))
    return files


def validate_schema_tree(root: Path) -> set[Path]:
    files = payload_files(root)
    if not files:
        fail(f"Descriptor schema directory is missing or empty: {root}")
    for relative in files:
        path = root / relative
        if path.suffix != ".json":
            fail(f"Descriptor schema is not JSON: {path}")
        load_json(path)
    return files


def copy_legal_files(source: Path, destination: Path, required: tuple[str, ...] = ()) -> list[str]:
    files = {path for path in source.rglob("*")
             if path.is_file() and path.name.lower().startswith(LEGAL_PREFIXES)}
    files.update(source / check_safe_relative(relative, "licence notice") for relative in required)
    if not files:
        fail(f"Required licence evidence is missing: {source}")
    copied = []
    for path in sorted(files):
        if not path.resolve().is_relative_to(source.resolve()) or not path.is_file() or not path.stat().st_size:
            fail(f"Invalid, missing or empty licence evidence: {path}")
        relative = path.relative_to(source)
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
        copied.append(relative.as_posix())
    return copied


def has_python_op_module(stage_root: Path) -> bool:
    return any(
        (stage_root / relative).is_file()
        for relative in (
            "lib/opk/opk-python-ops.so",
            "development/build/meson-out/opk-python-ops.so",
        )
    )


def stage_legal(args: argparse.Namespace) -> None:
    repo_root = Path(args.repo_root).resolve()
    deps_root = Path(args.deps_root).resolve()
    stage_root = Path(args.stage_root).resolve()
    legal_root = stage_root / "share/opk/licenses"
    legal_root.mkdir(parents=True, exist_ok=False)
    components = {}
    catalogue_path = repo_root / "scripts/release/third-party-licenses.json"
    catalogue = load_json(catalogue_path)
    if not isinstance(catalogue, dict):
        fail(f"Third-party licence catalogue must be an object: {catalogue_path}")

    def collect(name: str, version: str, source: Path) -> None:
        record = catalogue.get(name)
        if not isinstance(record, dict) or not all(
            isinstance(record.get(key), str) and record[key] for key in ("version", "licence", "repository")
        ):
            fail(f"Missing third-party licence information for {name}; update {catalogue_path}")
        if version != record["version"]:
            fail(f"{name} version changed from {record['version']} to {version}; "
                 f"review its licence and notices and update {catalogue_path}")
        required = tuple(relative for component, relative in SOURCE_LEGAL_NOTICES if component == name)
        notices = copy_legal_files(source, legal_root / name, required)
        components[name] = {
            **record,
            "notices": [f"{name}/{notice}" for notice in notices],
        }

    for name in ("LICENSE", "NOTICE", "THIRD_PARTY_NOTICE.md"):
        shutil.copyfile(repo_root / name, legal_root / name)
    shutil.copyfile(repo_root / "docs/public/licensing.md", legal_root / "README.md")
    components["opk"] = {
        "version": read_version(repo_root), "licence": "Apache-2.0",
        "repository": "https://github.com/arm/open-perception-kit",
        "notices": list(OPK_LEGAL_NOTICES),
    }
    subprojects = repo_root / "development/subprojects"
    if {path.stem for path in subprojects.glob("*.wrap")} != MESON_COMPONENTS:
        fail("Meson dependencies changed; update the licence inventory before releasing")
    for name in sorted(MESON_COMPONENTS):
        wrap = configparser.ConfigParser(interpolation=None)
        wrap.read(subprojects / f"{name}.wrap")
        directory = check_safe_relative(wrap["wrap-file"]["directory"], "wrap directory")
        collect(name, str(directory), subprojects / directory)

    build = load_json(repo_root / "requirements/build.json")
    for name in ("LICENSE", "ThirdPartyNotices.txt"):
        if not (deps_root / "onnxruntime/share/doc/onnxruntime" / name).is_file():
            fail(f"ONNX Runtime is missing required licence evidence: {name}")
    collect("onnxruntime", build["onnxruntime"], deps_root / "onnxruntime/share/doc/onnxruntime")
    sdk = load_json(repo_root / "tools/perception/sdk.json")
    vendor = repo_root / "development/web/content/vendor"
    collect("flatbuffers", sdk["flatbuffers"]["version"], vendor / "flatbuffers")
    fontawesome = vendor / "fontawesome"
    font_version = re.search(r"Font Awesome Free ([\d.]+)",
                             (fontawesome / "css/all.min.css").read_text(encoding="utf-8")[:256])
    if font_version is None:
        fail("Cannot identify the bundled Font Awesome version")
    collect("fontawesome", font_version.group(1), fontawesome)

    executorch = deps_root / "executorch-legal-documentation"
    if executorch.is_dir():
        version = subprocess.check_output(
            ["dpkg-query", "--show", "--showformat=${Version}", "libexecutorch-dev"], text=True).strip()
        collect("executorch", version, executorch)
    needs_numpy_headers = has_python_op_module(stage_root) and not args.include_python
    if needs_numpy_headers or args.include_python:
        for name in ("numpy", "flatbuffers"):
            if name == "flatbuffers" and not args.include_python:
                continue
            distribution = importlib.metadata.distribution(name)
            metadata_files = [entry for entry in distribution.files or ()
                              if ".dist-info/" in str(entry) and entry.name == "METADATA"]
            if len(metadata_files) != 1:
                fail(f"Cannot locate installed {name} distribution metadata")
            source = Path(distribution.locate_file(metadata_files[0])).parent
            collect(f"python-{name}" if args.include_python else "numpy-headers",
                    distribution.version, source)

    write_json(legal_root / "components.json", components)
    (legal_root / "THIRD_PARTY_LICENSES.md").write_text(legal_report(components), encoding="utf-8")
    validate_legal_documentation(stage_root, require_backends=False, require_python=args.include_python)


def legal_report(components: dict) -> str:
    lines = [
        "# Open Perception Kit licence report", "",
        f"Generated for OPK release: {components['opk']['version']}", "",
        "This report covers the collected component notices. A listed component may be a",
        "build dependency rather than part of every executable. Original texts are linked",
        "below and remain authoritative, including bundled dependencies' separate terms.", "",
        "| Component | Version or source revision | Repository | Licence | Original notices |",
        "| --- | --- | --- | --- | --- |",
    ]
    for name, component in sorted(components.items()):
        notices = ", ".join(f"[{path}](<{quote(path, safe='/')}>)" for path in component["notices"])
        cells = [name, component["version"], component["repository"], component["licence"], notices]
        lines.append("| " + " | ".join(cell.replace("|", r"\|").replace("\n", " ") for cell in cells) + " |")
    return "\n".join(lines) + "\n"


def validate_legal_documentation(
    package_root: Path, *, require_backends: bool = True, require_python: bool = False
) -> None:
    legal_root = package_root / "share/opk/licenses"
    inventory = load_json(legal_root / "components.json")
    required = CORE_LEGAL_COMPONENTS.copy()
    if require_backends:
        required.add("executorch")
    if require_python:
        required.update(("python-numpy", "python-flatbuffers"))
    if has_python_op_module(package_root) and not require_python:
        required.add("numpy-headers")
    if not isinstance(inventory, dict):
        fail("Packaged licence inventory must be an object")
    if required - inventory.keys():
        fail(f"Packaged licence inventory is missing required components: {sorted(required - inventory.keys())}")
    for name, component in inventory.items():
        if not isinstance(component, dict) or not all(
            isinstance(component.get(key), str) and component[key] for key in ("version", "licence", "repository")
        ):
            fail(f"Packaged licence record is incomplete: {name}")
        if not isinstance(component.get("notices"), list) or not component["notices"]:
            fail(f"Packaged licence notice list is invalid: {name}")
        for relative in component["notices"]:
            if not isinstance(relative, str):
                fail(f"Packaged licence notice path is invalid: {name}")
            path = legal_root / check_safe_relative(relative, "licence notice")
            if not path.resolve().is_relative_to(legal_root.resolve()) or not path.is_file() or not path.stat().st_size:
                fail(f"Packaged licence evidence is missing or empty: {name}/{relative}")
    for relative in OPK_LEGAL_NOTICES:
        if relative not in inventory["opk"]["notices"]:
            fail(f"Packaged licence notice list is missing required OPK notice: {relative}")
    for name, relative in SOURCE_LEGAL_NOTICES:
        if f"{name}/{relative}" not in inventory[name]["notices"]:
            fail(f"Packaged licence notice list is missing required upstream attribution: {name}/{relative}")
    for name in ("onnxruntime/LICENSE", "onnxruntime/ThirdPartyNotices.txt"):
        if not (legal_root / name).is_file() or not (legal_root / name).stat().st_size:
            fail(f"Packaged ONNX Runtime licence evidence is missing: {name}")
    report = legal_root / "THIRD_PARTY_LICENSES.md"
    if not report.is_file() or report.read_text(encoding="utf-8") != legal_report(inventory):
        fail("Packaged third-party licence report is missing or differs from components.json")


def validate_release_payload(package_root: Path, repo_root: Path | None) -> None:
    for path in (package_root / "share/opk/models").rglob("*"):
        if path.is_symlink() or (path.is_file() and path.suffix != ".json"):
            fail(f"Packaged model payload must contain only configuration files: {path}")
    packaged_schema_root = package_root / "share/opk/schemas/json/v1"
    validate_schema_tree(packaged_schema_root)
    if repo_root is None:
        return

    with tempfile.TemporaryDirectory() as temporary:
        expected_root = Path(temporary) / "expected"
        stage_models(
            argparse.Namespace(
                repo_root=str(repo_root),
                stage_root=str(expected_root),
            )
        )
        for payload in ("models", "opchains", "schemas/json/v1"):
            expected = expected_root / "share/opk" / payload
            packaged = package_root / "share/opk" / payload
            expected_files = payload_files(expected)
            packaged_files = payload_files(packaged)
            if packaged_files != expected_files:
                fail(f"Packaged {payload} payload does not match the selected source")
            for relative in expected_files:
                packaged_path = packaged / relative
                if packaged_path.suffix == ".json" and "/work/" in packaged_path.read_text(
                    encoding="utf-8"
                ):
                    fail(f"Build-machine path remains in {packaged_path}")
                if not filecmp.cmp(expected / relative, packaged_path, shallow=False):
                    fail(f"Packaged payload differs from the selected source: {packaged_path}")


def perception_sdk_archive(perception_sdk_root: Path) -> Path:
    if perception_sdk_root.is_symlink() or not perception_sdk_root.is_dir():
        fail(f"open-perception-kit directory is missing or invalid: {perception_sdk_root}")
    entries = list(perception_sdk_root.iterdir())
    if any(path.is_symlink() or not path.is_file() for path in entries):
        fail("open-perception-kit directory must contain only regular files")

    archives = [
        path
        for path in entries
        if PERCEPTION_SDK_ARCHIVE_PATTERN.fullmatch(path.name)
    ]
    if len(archives) != 1:
        fail("open-perception-kit directory must contain exactly one versioned ZIP")
    archive = archives[0]
    expected_names = {
        archive.name,
        f"{archive.name}.sha256",
        f"{archive.name}.provenance.json",
    }
    if {path.name for path in entries} != expected_names:
        fail("open-perception-kit directory must contain exactly the matching triplet")
    return archive


def validate_perception_sdk(
    perception_sdk_root: Path,
    expected_commit: str,
    repo_root: Path | None = None,
) -> None:
    archive = perception_sdk_archive(perception_sdk_root)
    version = archive.name.removeprefix("open-perception-kit-").removesuffix(".zip")

    verification_root = repo_root or REPO_ROOT
    if repo_root is not None and read_version(repo_root) != version:
        fail("open-perception-kit version does not match the selected source")
    subprocess.run(
        [
            str(verification_root / "scripts/perception-sdk.sh"),
            "verify",
            str(archive),
            "--require-sidecars",
        ],
        check=True,
        cwd=verification_root,
    )

    provenance = load_json(perception_sdk_root / f"{archive.name}.provenance.json")
    if not isinstance(provenance, dict) or provenance.get("dirty") is not False:
        fail("open-perception-kit provenance must record dirty=false")
    commit = provenance.get("repository_commit")
    if not isinstance(commit, str) or not GIT_COMMIT_PATTERN.fullmatch(commit):
        fail("open-perception-kit provenance commit is invalid")
    if not GIT_COMMIT_PATTERN.fullmatch(expected_commit):
        fail("Expected open-perception-kit commit is invalid")
    if commit != expected_commit:
        fail("open-perception-kit provenance commit does not match the selected source")


def read_elf(path: Path, *arguments: str) -> str:
    completed = subprocess.run(
        ["readelf", *arguments, str(path)],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return completed.stdout


def is_elf(path: Path) -> bool:
    if not path.is_file() or path.is_symlink():
        return False
    with path.open("rb") as stream:
        return stream.read(4) == b"\x7fELF"


def dynamic_values(path: Path, tag: str) -> list[str]:
    pattern = re.compile(rf"\({re.escape(tag)}\).*\[([^\]]+)\]")
    return [
        match.group(1)
        for line in read_elf(path, "-dW").splitlines()
        if (match := pattern.search(line))
    ]


def validate_release_tree(package_root: Path) -> None:
    forbidden_parts = {
        "examples",
        "tests",
        "pipelines",
        "src",
        "include",
    }
    legal_root = package_root / "share/opk/licenses"
    for path in package_root.rglob("*"):
        relative = path.relative_to(package_root)
        if path.name.casefold().endswith((".onnx", ".onnx.part", ".pte", ".pte.part", ".bin", ".bin.part")):
            fail(f"Forbidden model binary in release: {relative}")
        if legal_root in path.parents:
            continue
        if (
            relative.parts[:3] == ("share", "opk", "python")
            and relative.parts[3:]
            not in ((), ("opk_python_ops.pyi",), ("pyproject.toml",), ("requirements.txt",))
        ):
            fail(f"Redistributed Python package in release: {relative}")
        if forbidden_parts & set(relative.parts):
            fail(f"Forbidden release path: {relative}")
        if path.suffix.casefold() in RETIRED_RELEASE_SUFFIXES or any(
            marker in part.casefold()
            for marker in RETIRED_RELEASE_PATH_MARKERS
            for part in relative.parts
        ):
            fail(f"Forbidden retired release path: {relative}")
        if path.name == "opk-menu" or path.name.startswith("libfmt.so"):
            fail(f"Forbidden release file: {relative}")
        if path.is_file() and path.suffix.lower() in {".a", ".h", ".hh", ".hpp"}:
            fail(f"Forbidden SDK file: {relative}")


def validate_runtime_files(package_root: Path) -> Path:
    plugin_root = package_root / "lib/gstreamer-1.0"
    if not plugin_root.is_dir():
        fail("Plugin directory is missing")
    plugins = list(plugin_root.iterdir())
    plugin_names = {path.name for path in plugins}
    if plugin_names != PLUGIN_NAMES:
        fail(f"Plugin directory must contain exactly six plugins: {sorted(plugin_names)}")
    if any(not is_elf(path) for path in plugins):
        fail("GStreamer plugins must be regular ELF files")

    validate_release_tree(package_root)
    model_root = package_root / "share/opk/models"
    if not model_root.is_dir() or model_root.is_symlink():
        fail("Packaged model directory is missing or invalid")
    model_entries = list(model_root.iterdir())
    if any(not path.is_dir() or path.is_symlink() for path in model_entries):
        fail("Packaged models must be directories")
    model_names = {path.name for path in model_entries}
    if model_names != RELEASE_MODEL_NAMES:
        fail(f"Packaged models must be exactly {sorted(RELEASE_MODEL_NAMES)}")

    private_root = package_root / "lib/opk"
    op_modules = list(private_root.glob("opk-*-ops.so"))
    if {path.name for path in op_modules} != OP_MODULE_NAMES:
        fail(f"OPK op modules must be exactly {sorted(OP_MODULE_NAMES)}")
    if any(not is_elf(path) for path in op_modules):
        fail("OPK op modules must be regular ELF files")

    runtime_library = private_root / RUNTIME_LIBRARY_NAME
    if not is_elf(runtime_library) or any(
        private_root.glob(f"{RUNTIME_LIBRARY_NAME}.*")
    ):
        fail("Packaged OPK runtime library is missing or invalid")

    common_library = private_root / "libopk-common.so"
    if not is_elf(common_library) or any(private_root.glob("libopk-common.so.*")):
        fail("Packaged OPK common library is missing or invalid")
    python_stub = package_root / "share/opk/python/opk_python_ops.pyi"
    if not python_stub.is_file() or python_stub.is_symlink():
        fail("Packaged PythonScript type stub is missing or invalid")
    return private_root


def onnxruntime_archive_source(descriptor: Path, architecture: str) -> dict[str, str]:
    config = json_mapping(descriptor)
    version = config["onnxruntime"]
    archive_arch = {"x86_64": "x64", "aarch64": "aarch64"}[architecture]
    archive = f"onnxruntime-linux-{archive_arch}-{version}.tgz"
    return {
        "archive_url": f"https://github.com/microsoft/onnxruntime/releases/download/v{version}/{archive}",
        "archive_sha256": config[f"onnxruntime-sha256-{archive_arch}"],
        "library": f"libonnxruntime.so.{version}",
    }


def validate_onnxruntime_provenance(
    package_root: Path, repo_root: Path | None, architecture: str
) -> None:
    private_root = package_root / "lib/opk"
    library = private_root / "libonnxruntime.so.1.24.4"
    receipt_path = library.with_name(f"{library.name}.provenance.json")
    if not receipt_path.is_file() or receipt_path.is_symlink():
        fail("Packaged ONNX Runtime provenance receipt is missing or invalid")
    receipt = json_mapping(receipt_path)
    if receipt.get("library") != library.name or receipt.get("library_sha256") != hashlib.sha256(library.read_bytes()).hexdigest():
        fail("Packaged ONNX Runtime library SHA-256 mismatch")
    if repo_root is not None:
        expected = onnxruntime_archive_source(repo_root / "requirements/build.json", architecture)
        if {key: receipt.get(key) for key in expected} != expected:
            fail("Packaged ONNX Runtime source does not match the selected source")
    else:
        if not isinstance(receipt.get("archive_url"), str) or not receipt["archive_url"].startswith("https://"):
            fail("Packaged ONNX Runtime archive URL is invalid")
        if not isinstance(receipt.get("archive_sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", receipt["archive_sha256"]):
            fail("Packaged ONNX Runtime archive SHA-256 is invalid")
    legal_root = package_root / "share/opk/licenses/onnxruntime"
    for filename in ("LICENSE", "ThirdPartyNotices.txt"):
        path = legal_root / filename
        if not path.is_file() or path.is_symlink() or not path.stat().st_size:
            fail(f"Packaged ONNX Runtime legal file is missing or invalid: {filename}")


def validate_onnx_runtime(private_root: Path) -> None:
    regular_onnx = [
        path
        for path in private_root.glob("libonnxruntime.so.*")
        if path.is_file() and not path.is_symlink()
        and not path.name.endswith(".provenance.json")
    ]
    soname_link = private_root / "libonnxruntime.so.1"
    if [path.name for path in regular_onnx] != ["libonnxruntime.so.1.24.4"]:
        fail("Package must contain exactly ONNX Runtime 1.24.4")
    if not soname_link.is_symlink() or os.readlink(soname_link) != regular_onnx[0].name:
        fail("ONNX Runtime SONAME link is missing or incorrect")
    if dynamic_values(regular_onnx[0], "SONAME") != ["libonnxruntime.so.1"]:
        fail("Pinned ONNX Runtime has an unexpected SONAME")


def validate_python_operation_runtime(private_root: Path) -> None:
    python_operation = private_root / "opk-python-ops.so"
    python_libraries = sorted(
        library
        for library in dynamic_values(python_operation, "NEEDED")
        if library.startswith("libpython")
    )
    if python_libraries != ["libpython3.13.so.1.0"]:
        fail(
            "Python operation must depend on exactly libpython3.13.so.1.0: "
            f"{python_libraries}"
        )


def validate_elf_dependency(
    path: Path,
    library: str,
    packaged_library_paths: dict[str, list[Path]],
    internal_search_directories: set[Path],
) -> None:
    if library == "libfmt.so" or library.startswith("libfmt.so."):
        fail(f"{path} has forbidden dependency {library}")
    if library not in packaged_library_paths and not library.startswith(
        SYSTEM_LIBRARY_PREFIXES
    ):
        fail(f"{path} has unresolved or unclassified dependency {library}")
    if library in packaged_library_paths and not any(
        (directory / library).is_file() for directory in internal_search_directories
    ):
        fail(f"{path} cannot resolve packaged dependency {library} through its RUNPATH")


def validate_elf(
    path: Path,
    package_root: Path,
    expected_machine: str,
    packaged_library_paths: dict[str, list[Path]],
) -> None:
    header = read_elf(path, "-hW")
    machine = next(
        (
            line.partition(":")[2].strip()
            for line in header.splitlines()
            if line.strip().startswith("Machine:")
        ),
        "",
    )
    if machine != expected_machine:
        fail(f"Wrong ELF architecture: {path}")
    runpaths = dynamic_values(path, "RUNPATH")
    search_paths = runpaths + dynamic_values(path, "RPATH")
    internal_search_directories = {
        (path.parent / entry.replace("$ORIGIN", str(path.parent))).resolve()
        for search_path in search_paths
        for entry in search_path.split(":")
        if entry
    }
    for library in dynamic_values(path, "NEEDED"):
        validate_elf_dependency(
            path, library, packaged_library_paths, internal_search_directories
        )

    relative = path.relative_to(package_root)
    if relative.parts[:2] == ("lib", "gstreamer-1.0"):
        expected_runpath = "$ORIGIN/../opk"
    elif relative.parts[:2] == ("lib", "opk") and len(relative.parts) == 3:
        expected_runpath = "$ORIGIN"
    else:
        expected_runpath = ""
    if expected_runpath and expected_runpath not in runpaths:
        fail(f"{path} has RUNPATH {runpaths}, expected {expected_runpath}")


def validate_package(args: argparse.Namespace) -> None:
    package_root = Path(args.package_root).resolve()
    architecture = args.architecture
    repo_root_value = getattr(args, "repo_root", None)
    repo_root = Path(repo_root_value).resolve() if repo_root_value else None
    private_root = validate_runtime_files(package_root)
    validate_legal_documentation(package_root)
    validate_release_payload(package_root, repo_root)
    validate_perception_sdk(
        package_root / "share/opk/open-perception-kit",
        args.expected_commit,
        repo_root,
    )
    validate_onnx_runtime(private_root)
    validate_onnxruntime_provenance(package_root, repo_root, architecture)
    validate_python_operation_runtime(private_root)

    elf_paths = [path for path in package_root.rglob("*") if is_elf(path)]
    if not elf_paths:
        fail("Package contains no ELF objects")
    packaged_library_paths: dict[str, list[Path]] = {}
    for packaged_path in package_root.rglob("*"):
        if packaged_path.is_file():
            packaged_library_paths.setdefault(packaged_path.name, []).append(packaged_path)
    expected_machine = (
        "Advanced Micro Devices X86-64" if architecture == "x86_64" else "AArch64"
    )
    for path in elf_paths:
        validate_elf(path, package_root, expected_machine, packaged_library_paths)


def read_version(repo_root: Path) -> str:
    content = (repo_root / "development/meson.build").read_text(encoding="utf-8")
    match = re.search(r"project\([^)]*version:\s*'([^']+)'", content, re.DOTALL)
    if not match or not VERSION_PATTERN.fullmatch(match.group(1)):
        fail("development/meson.build must contain a stable MAJOR.MINOR.PATCH version")
    return match.group(1)


def read_package_versions(repo_root: Path, version: str) -> tuple[str, str]:
    paths = {
        "Python": repo_root / "generated/open_perception_kit/python/pyproject.toml",
        "Cargo": repo_root / "generated/open_perception_kit/rust/Cargo.toml",
    }
    versions: dict[str, str] = {}
    for language, path in paths.items():
        match = re.search(
            r'^version\s*=\s*"([^"]+)"',
            path.read_text(encoding="utf-8"),
            re.MULTILINE,
        )
        if match is None:
            fail(f"Generated Perception {language} package version is missing")
        versions[language] = match.group(1)
    result = (versions["Python"], versions["Cargo"])
    if result != (version, version):
        fail("Generated Perception package versions do not match the product version")
    return result


def changelog_section(repo_root: Path, version: str) -> str:
    content = (repo_root / "CHANGELOG.md").read_text(encoding="utf-8")
    match = re.search(
        rf"^## \[{re.escape(version)}\][^\n]*\n(?P<body>.*?)(?=^## \[|\Z)",
        content,
        re.MULTILINE | re.DOTALL,
    )
    if not match or not match.group("body").strip():
        fail(f"CHANGELOG.md has no non-empty {version} section")
    return match.group(0).strip()


def write_github_output(values: dict[str, str]) -> None:
    output_path = os.environ.get("GITHUB_OUTPUT")
    if not output_path:
        for key, value in values.items():
            print(f"{key}={value}")
        return
    with Path(output_path).open("a", encoding="utf-8") as stream:
        for key, value in values.items():
            stream.write(f"{key}={value}\n")


def prepare(args: argparse.Namespace) -> None:
    repo_root = Path(args.repo_root).resolve()
    version = read_version(repo_root)
    python_package_version, cargo_package_version = read_package_versions(
        repo_root, version
    )
    changelog_section(repo_root, version)
    commit = args.commit
    if not GIT_COMMIT_PATTERN.fullmatch(commit):
        fail("Source commit must be a full SHA")
    write_github_output(
        {
            "version": version,
            "python_package_version": python_package_version,
            "cargo_package_version": cargo_package_version,
            "commit": commit,
            "x86_archive": f"opk-{version}-linux-x86_64.tar.gz",
            "arm_archive": f"opk-{version}-linux-aarch64.tar.gz",
        }
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    stage_models_parser = subparsers.add_parser("stage-models")
    stage_models_parser.add_argument("--repo-root", default=".")
    stage_models_parser.add_argument("--stage-root", required=True)

    stage_legal_parser = subparsers.add_parser("stage-legal")
    stage_legal_parser.add_argument("--repo-root", default=".")
    stage_legal_parser.add_argument("--deps-root", default="/opt/opk-deps")
    stage_legal_parser.add_argument("--stage-root", required=True)
    stage_legal_parser.add_argument("--include-python", action="store_true")

    validate_legal_parser = subparsers.add_parser("validate-legal")
    validate_legal_parser.add_argument("--package-root", required=True)
    validate_legal_parser.add_argument("--require-backends", action="store_true")
    validate_legal_parser.add_argument("--require-python", action="store_true")

    validate_package_parser = subparsers.add_parser("validate-package")
    validate_package_parser.add_argument("--architecture", choices=sorted(ARCHITECTURES), required=True)
    validate_package_parser.add_argument("--package-root", required=True)
    validate_package_parser.add_argument("--repo-root")
    validate_package_parser.add_argument("--expected-commit", required=True)

    prepare_parser = subparsers.add_parser("prepare")
    prepare_parser.add_argument("--repo-root", default=".")
    prepare_parser.add_argument("--commit", required=True)

    args = parser.parse_args()
    try:
        if args.command == "stage-models":
            stage_models(args)
        elif args.command == "stage-legal":
            stage_legal(args)
        elif args.command == "validate-legal":
            validate_legal_documentation(
                Path(args.package_root), require_backends=args.require_backends, require_python=args.require_python
            )
        elif args.command == "validate-package":
            validate_package(args)
        elif args.command == "prepare":
            prepare(args)
        return 0
    except (OSError, RuntimeError, subprocess.CalledProcessError, ValueError) as error:
        print(f"release error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
