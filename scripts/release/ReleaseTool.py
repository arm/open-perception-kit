#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

"""Build-time checks and staging for the three OPK release archives."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import filecmp
import importlib.metadata
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ARCHITECTURES = {"x86_64", "aarch64"}
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
PYTHON_RUNTIME_DISTRIBUTIONS = {
    "flatbuffers",
    "numpy",
    "open-perception-kit",
}
PYTHON_RUNTIME_MODULES = {
    "flatbuffers",
    "numpy",
    "open_perception_kit",
}
PYTHON_RUNTIME_ROOT = Path("share/opk/python")
PYTHON_RUNTIME_MANIFEST = "opk-runtime.json"
PYTHON_OPS_TYPE_STUB = "opk_python_ops.pyi"
PYTHON_RUNTIME_EXCLUDED_PARTS = {
    "__pycache__",
    "examples",
    "include",
    "src",
    "test",
    "tests",
}
PYTHON_RUNTIME_EXCLUDED_SUFFIXES = {".a", ".h", ".hh", ".hpp", ".pyc", ".pyo"}
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
    "libpython3.14.so.",
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
BUILD_LABEL_PATTERN = re.compile(r"^[a-z0-9](?:[a-z0-9-]{0,30}[a-z0-9])?$")
VERSION_PATTERN = re.compile(r"^\d+\.\d+\.\d+$", re.ASCII)
JSON_GLOB = "*.json"
REPO_ROOT = Path(__file__).resolve().parents[2]

GIT_COMMIT_PATTERN = re.compile(r"^[0-9a-f]{40}$", re.ASCII)
PERCEPTION_SDK_ARCHIVE_PATTERN = re.compile(
    r"^open-perception-kit-sdk-(\d+\.\d+\.\d+)\.zip$", re.ASCII
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
) -> tuple[list[Path], list[Path]]:
    config_paths = sorted(model_root.rglob(JSON_GLOB))
    model_suffix = RELEASE_MODELS[model_id][1]
    model_paths = {
        resolve_model_path(
            model_root, str(path.relative_to(model_root)), f"{model_id}.{path.name}"
        )
        for path in model_root.rglob("*")
        if path.is_file() and path.suffix == model_suffix
    }
    for config_path in config_paths:
        config = load_json(config_path)
        if not isinstance(config, dict):
            fail(f"{model_id}: invalid model config: {config_path.name}")
        if "modelFile" not in config:
            continue
        model_path = resolve_model_path(
            model_root, config["modelFile"], f"{model_id}.{config_path.name}.modelFile"
        )
        if not model_path.is_file():
            fail(f"{model_id}: resolved model is missing: {model_path}")
        if model_path not in model_paths:
            fail(f"{model_id}: unsupported model file: {model_path.name}")
    if primary_descriptor not in config_paths or not model_paths:
        fail(f"{model_id}: modelDescriptor is not a model config")
    return config_paths, sorted(model_paths)


def discover_models(repo_root: Path) -> dict[str, dict[str, object]]:
    models_root = repo_root / "config/models"
    if not models_root.is_dir():
        fail(f"Model directory does not exist: {models_root}")
    models: dict[str, dict[str, object]] = {}
    for model_id in sorted(RELEASE_MODEL_NAMES):
        model_root = models_root / model_id
        if not model_root.is_dir():
            fail(f"Release model directory does not exist: {model_root}")
        config_paths, model_paths = collect_model_files(
            model_root, model_id, find_primary_descriptor(model_root, model_id)
        )
        models[model_id] = {
            "config_paths": config_paths,
            "model_paths": model_paths,
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
        for source_path in entry["model_paths"]:
            destination_path = model_root / source_path.relative_to(source_root)
            destination_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source_path, destination_path)
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


def canonical_distribution_name(name: str) -> str:
    return re.sub(r"[-_.]+", "-", name).lower()


def stage_python_distribution(
    distribution: importlib.metadata.Distribution,
    target_root: Path,
) -> int:
    source_root = Path(distribution.locate_file("")).resolve()
    copied = 0
    for entry in distribution.files or ():
        source = Path(distribution.locate_file(entry)).resolve()
        try:
            relative = source.relative_to(source_root)
        except ValueError:
            continue
        if (
            PYTHON_RUNTIME_EXCLUDED_PARTS & set(relative.parts)
            or source.suffix.lower() in PYTHON_RUNTIME_EXCLUDED_SUFFIXES
            or source.name in {"RECORD", "direct_url.json"}
            or not source.is_file()
        ):
            continue
        destination = target_root / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.exists():
            if not filecmp.cmp(source, destination, shallow=False):
                fail(f"Python runtime distribution collision: {relative}")
            continue
        shutil.copy2(source, destination)
        copied += 1
    return copied


def clear_python_runtime(target_root: Path) -> None:
    for path in target_root.iterdir():
        if path.name == PYTHON_OPS_TYPE_STUB:
            continue
        if path.is_dir() and not path.is_symlink():
            shutil.rmtree(path)
        else:
            path.unlink()


def installed_python_runtime_distribution(
    requested_name: str,
) -> tuple[str, importlib.metadata.Distribution]:
    try:
        distribution = importlib.metadata.distribution(requested_name)
    except importlib.metadata.PackageNotFoundError:
        fail(f"Python runtime distribution is not installed: {requested_name}")
    distribution_name = distribution.metadata["Name"]
    if not distribution_name:
        fail(f"Python runtime distribution has no name: {requested_name}")
    name = canonical_distribution_name(distribution_name)
    if name != requested_name:
        fail(f"Unexpected Python runtime distribution: {name}")
    return name, distribution


def stage_python_runtime(args: argparse.Namespace) -> None:
    target_root = Path(args.stage_root).resolve() / PYTHON_RUNTIME_ROOT
    target_root.mkdir(parents=True, exist_ok=True)
    clear_python_runtime(target_root)
    versions: dict[str, str] = {}
    for requested_name in sorted(PYTHON_RUNTIME_DISTRIBUTIONS):
        name, distribution = installed_python_runtime_distribution(requested_name)
        if stage_python_distribution(distribution, target_root) == 0:
            fail(f"Python runtime distribution has no package files: {name}")
        versions[name] = distribution.version
    (target_root / PYTHON_RUNTIME_MANIFEST).write_text(
        json.dumps({"distributions": versions}, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def json_mapping(path: Path) -> dict[str, object]:
    value = load_json(path)
    if not isinstance(value, dict):
        fail(f"Expected a JSON object in {path}")
    return value


def configured_version(config: dict[str, object], package_name: str, path: Path) -> str:
    package = config.get(package_name)
    if not isinstance(package, dict):
        fail(f"Missing {package_name} configuration in {path}")
    version = package.get("version")
    if not isinstance(version, str) or not version:
        fail(f"Missing {package_name} version in {path}")
    return version


def expected_python_runtime_versions(repo_root: Path) -> dict[str, str]:
    runtime_path = repo_root / "development/ops-python/runtime.json"
    sdk_path = repo_root / "tools/perception/sdk.json"
    runtime = json_mapping(runtime_path)
    sdk = json_mapping(sdk_path)
    pyproject = (
        repo_root / "generated/perception/python/pyproject.toml"
    ).read_text(encoding="utf-8")
    version_match = re.search(r'^version\s*=\s*"([^"]+)"', pyproject, re.MULTILINE)
    if version_match is None:
        fail("Generated Perception Python package version is missing")
    return {
        "flatbuffers": configured_version(sdk, "flatbuffers", sdk_path),
        "numpy": configured_version(runtime, "numpy", runtime_path),
        "open-perception-kit": version_match.group(1),
    }


def validate_python_runtime(package_root: Path, repo_root: Path | None = None) -> None:
    runtime_root = package_root / PYTHON_RUNTIME_ROOT
    if not runtime_root.is_dir() or runtime_root.is_symlink():
        fail("Packaged Python runtime is missing or invalid")
    missing_modules = sorted(
        module for module in PYTHON_RUNTIME_MODULES if not (runtime_root / module).is_dir()
    )
    if missing_modules:
        fail(f"Packaged Python runtime modules are missing: {missing_modules}")
    allowed_prefixes = ("flatbuffers", "numpy", "open_perception_kit")
    allowed_files = {PYTHON_OPS_TYPE_STUB, PYTHON_RUNTIME_MANIFEST}
    unexpected = sorted(
        path.name
        for path in runtime_root.iterdir()
        if path.name not in allowed_files
        and not path.name.startswith(allowed_prefixes)
    )
    if unexpected:
        fail(f"Packaged Python runtime contains unexpected entries: {unexpected}")
    type_stub = runtime_root / PYTHON_OPS_TYPE_STUB
    if not type_stub.is_file() or type_stub.is_symlink():
        fail("Packaged Python operation type stub is missing or invalid")
    manifest_path = runtime_root / PYTHON_RUNTIME_MANIFEST
    if not manifest_path.is_file() or manifest_path.is_symlink():
        fail("Packaged Python runtime manifest is missing or invalid")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    distributions = manifest.get("distributions")
    if not isinstance(distributions, dict) or set(distributions) != PYTHON_RUNTIME_DISTRIBUTIONS:
        fail("Packaged Python runtime distributions do not match the release contract")
    if any(not isinstance(version, str) or not version for version in distributions.values()):
        fail("Packaged Python runtime contains an invalid distribution version")
    if repo_root is not None and distributions != expected_python_runtime_versions(repo_root):
        fail("Packaged Python runtime versions do not match the selected source")


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


def validate_legal_documentation(package_root: Path) -> None:
    legal_root = package_root / "share/opk/licenses"
    if not payload_files(legal_root):
        fail("Packaged legal documentation is missing or empty")
    if not payload_files(legal_root / "libexecutorch-dev"):
        fail("Packaged ExecuTorch legal documentation is missing or empty")


def validate_release_payload(package_root: Path, repo_root: Path | None) -> None:
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
        fail(f"Perception SDK directory is missing or invalid: {perception_sdk_root}")
    entries = list(perception_sdk_root.iterdir())
    if any(path.is_symlink() or not path.is_file() for path in entries):
        fail("Perception SDK directory must contain only regular files")

    archives = [
        path
        for path in entries
        if PERCEPTION_SDK_ARCHIVE_PATTERN.fullmatch(path.name)
    ]
    if len(archives) != 1:
        fail("Perception SDK directory must contain exactly one versioned ZIP")
    archive = archives[0]
    expected_names = {
        archive.name,
        f"{archive.name}.sha256",
        f"{archive.name}.provenance.json",
    }
    if {path.name for path in entries} != expected_names:
        fail("Perception SDK directory must contain exactly the matching triplet")
    return archive


def validate_perception_sdk(
    perception_sdk_root: Path,
    expected_commit: str,
    repo_root: Path | None = None,
) -> None:
    archive = perception_sdk_archive(perception_sdk_root)
    version = archive.name.removeprefix("open-perception-kit-sdk-").removesuffix(".zip")

    verification_root = repo_root or REPO_ROOT
    if repo_root is not None and read_version(repo_root) != version:
        fail("Perception SDK version does not match the selected source")
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
        fail("Perception SDK provenance must record dirty=false")
    commit = provenance.get("repository_commit")
    if not isinstance(commit, str) or not GIT_COMMIT_PATTERN.fullmatch(commit):
        fail("Perception SDK provenance commit is invalid")
    if not GIT_COMMIT_PATTERN.fullmatch(expected_commit):
        fail("Expected Perception SDK commit is invalid")
    if commit != expected_commit:
        fail("Perception SDK provenance commit does not match the selected source")


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
        if legal_root in path.parents:
            continue
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
    return private_root


def validate_onnx_runtime(private_root: Path) -> None:
    regular_onnx = [
        path
        for path in private_root.glob("libonnxruntime.so.*")
        if path.is_file() and not path.is_symlink()
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
    if python_libraries != ["libpython3.14.so.1.0"]:
        fail(
            "Python operation must depend on exactly libpython3.14.so.1.0: "
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
        package_root / "share/opk/open-perception-kit-sdk",
        args.expected_commit,
        repo_root,
    )
    validate_python_runtime(package_root, repo_root)
    validate_onnx_runtime(private_root)
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
        "Python": repo_root / "generated/perception/python/pyproject.toml",
        "Cargo": repo_root / "generated/perception/rust/Cargo.toml",
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
    expected = {
        (version, version),
        (f"{version}.dev0", f"{version}-dev.0"),
    }
    result = (versions["Python"], versions["Cargo"])
    if result not in expected:
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


def prerelease_identity(
    commit: str,
    run_id: str,
    run_attempt: str,
    now: datetime | None = None,
) -> dict[str, str]:
    if not GIT_COMMIT_PATTERN.fullmatch(commit):
        fail("Source commit must be a full SHA")
    if not re.fullmatch(r"[1-9]\d*", run_id):
        fail("GitHub run ID must be a positive integer")
    if not re.fullmatch(r"[1-9]\d*", run_attempt):
        fail("GitHub run attempt must be a positive integer")
    timestamp = (now or datetime.now(timezone.utc)).astimezone(timezone.utc)
    version = (
        f"{timestamp:%Y%m%d}."
        f"{1_000_000 + int(timestamp.strftime('%H%M%S'))}."
        f"{int(run_id) * 1000 + int(run_attempt)}"
    )
    return {
        "version": version,
        "build_label": f"prerelease-g{commit[:12]}",
        "source_branch": f"sandbox/prerelease-source/{run_id}-{run_attempt}",
    }


def prepare_prerelease(args: argparse.Namespace) -> None:
    write_github_output(
        prerelease_identity(args.commit, args.run_id, args.run_attempt)
    )


def prepare(args: argparse.Namespace) -> None:
    repo_root = Path(args.repo_root).resolve()
    version = read_version(repo_root)
    python_package_version, cargo_package_version = read_package_versions(
        repo_root, version
    )
    if not args.build_label:
        changelog_section(repo_root, version)
        if (python_package_version, cargo_package_version) != (version, version):
            fail("Stable releases require stable Perception package versions")
    elif (python_package_version, cargo_package_version) == (version, version):
        fail("Prereleases require prerelease Perception package versions")
    commit = args.commit
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        fail("Source commit must be a full SHA")
    if args.build_label:
        if not BUILD_LABEL_PATTERN.fullmatch(args.build_label):
            fail("build_label does not match the required policy")
        build_id = f"{version}-{args.build_label}-{commit[:12]}"
    else:
        build_id = version
    write_github_output(
        {
            "version": version,
            "python_package_version": python_package_version,
            "cargo_package_version": cargo_package_version,
            "commit": commit,
            "build_id": build_id,
            "x86_archive": f"opk-{build_id}-linux-x86_64.tar.gz",
            "arm_archive": f"opk-{build_id}-linux-aarch64.tar.gz",
            "docs_archive": f"opk-docs-{build_id}.tar.gz",
        }
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    stage_models_parser = subparsers.add_parser("stage-models")
    stage_models_parser.add_argument("--repo-root", default=".")
    stage_models_parser.add_argument("--stage-root", required=True)

    stage_python_runtime_parser = subparsers.add_parser("stage-python-runtime")
    stage_python_runtime_parser.add_argument("--stage-root", required=True)

    validate_package_parser = subparsers.add_parser("validate-package")
    validate_package_parser.add_argument("--architecture", choices=sorted(ARCHITECTURES), required=True)
    validate_package_parser.add_argument("--package-root", required=True)
    validate_package_parser.add_argument("--repo-root")
    validate_package_parser.add_argument("--expected-commit", required=True)

    prepare_parser = subparsers.add_parser("prepare")
    prepare_parser.add_argument("--repo-root", default=".")
    prepare_parser.add_argument("--commit", required=True)
    prepare_parser.add_argument("--build-label", default="")

    prerelease_parser = subparsers.add_parser("prepare-prerelease")
    prerelease_parser.add_argument("--commit", required=True)
    prerelease_parser.add_argument("--run-id", required=True)
    prerelease_parser.add_argument("--run-attempt", required=True)

    args = parser.parse_args()
    try:
        if args.command == "stage-models":
            stage_models(args)
        elif args.command == "stage-python-runtime":
            stage_python_runtime(args)
        elif args.command == "validate-package":
            validate_package(args)
        elif args.command == "prepare":
            prepare(args)
        elif args.command == "prepare-prerelease":
            prepare_prerelease(args)
        return 0
    except (OSError, RuntimeError, subprocess.CalledProcessError, ValueError) as error:
        print(f"release error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
