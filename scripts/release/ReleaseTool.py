#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

"""Build-time checks and staging for the three PEK release archives."""

from __future__ import annotations

import argparse
import filecmp
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ARCHITECTURES = {"x86_64", "aarch64"}
RELEASE_MODEL_NAMES = {
    "cam-contact",
    "gaze-detection",
    "osnet_x0_25",
    "ultraface",
    "yolo26",
    "yolov11",
}
PLUGIN_NAMES = {
    "libpekcomm.so",
    "libpekinfer.so",
    "libpekosd.so",
    "libpekperformance.so",
    "libpeksink.so",
    "libpektracker.so",
}
OP_MODULE_NAMES = {
    "pek-onnx-ops.so",
    "pek-std-ops.so",
}
RUNTIME_LIBRARY_NAME = "pek-runtime.so"
SYSTEM_LIBRARY_PREFIXES = (
    "ld-linux-",
    "libblkid.so.",
    "libbrotlicommon.so.",
    "libbrotlidec.so.",
    "libbrotlienc.so.",
    "libc.so.",
    "libcairo-gobject.so.",
    "libcairo.so.",
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

    inference_ops = []
    for op in opchain["ops"]:
        if not isinstance(op, dict):
            fail(f"{model_id}: invalid op entry")
        if str(op.get("id", "")).startswith("pek-onnx-ops/"):
            inference_ops.append(op)
    if len(inference_ops) != 1:
        fail(f"{model_id}: model opchain must contain exactly one ONNX inference op")
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
    model_paths = {
        resolve_model_path(
            model_root, str(path.relative_to(model_root)), f"{model_id}.{path.name}"
        )
        for path in model_root.rglob("*")
        if path.is_file() and path.suffix == ".onnx"
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
        target_descriptor = stage_root / "share/pek/models" / model_id / descriptor_name
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
        model_root = stage_root / "share/pek/models" / model_id
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
    target_root = stage_root / "share/pek/opchains"
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

    for json_path in (stage_root / "share/pek").rglob(JSON_GLOB):
        content = json_path.read_text(encoding="utf-8")
        if "/work/" in content:
            fail(f"Build-machine path remains in {json_path}")


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


def validate_release_payload(package_root: Path, repo_root: Path) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        expected_root = Path(temporary) / "expected"
        stage_models(
            argparse.Namespace(
                repo_root=str(repo_root),
                stage_root=str(expected_root),
            )
        )
        for payload in ("models", "opchains"):
            expected = expected_root / "share/pek" / payload
            packaged = package_root / "share/pek" / payload
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

    forbidden_parts = {
        "examples",
        "tests",
        "pipelines",
        "src",
        "include",
    }
    for path in package_root.rglob("*"):
        relative = path.relative_to(package_root)
        if forbidden_parts & set(relative.parts):
            fail(f"Forbidden release path: {relative}")
        if any("hailo" in part.lower() for part in relative.parts):
            fail(f"Forbidden Hailo release path: {relative}")
        if path.name == "pek-menu" or path.name.startswith(
            ("libfmt.so", "pek-ncnn-ops.so")
        ):
            fail(f"Forbidden release file: {relative}")

    model_root = package_root / "share/pek/models"
    if not model_root.is_dir() or model_root.is_symlink():
        fail("Packaged model directory is missing or invalid")
    model_entries = list(model_root.iterdir())
    if any(not path.is_dir() or path.is_symlink() for path in model_entries):
        fail("Packaged models must be directories")
    model_names = {path.name for path in model_entries}
    if model_names != RELEASE_MODEL_NAMES:
        fail(f"Packaged models must be exactly {sorted(RELEASE_MODEL_NAMES)}")

    private_root = package_root / "lib/pek"
    op_modules = list(private_root.glob("pek-*-ops.so"))
    if {path.name for path in op_modules} != OP_MODULE_NAMES:
        fail(f"PEK op modules must be exactly {sorted(OP_MODULE_NAMES)}")
    if any(not is_elf(path) for path in op_modules):
        fail("PEK op modules must be regular ELF files")

    runtime_library = private_root / RUNTIME_LIBRARY_NAME
    if not is_elf(runtime_library) or any(
        private_root.glob(f"{RUNTIME_LIBRARY_NAME}.*")
    ):
        fail("Packaged PEK runtime library is missing or invalid")

    common_library = private_root / "libpek-common.so"
    if not is_elf(common_library) or any(private_root.glob("libpek-common.so.*")):
        fail("Packaged PEK common library is missing or invalid")
    return private_root


def validate_package(args: argparse.Namespace) -> None:
    package_root = Path(args.package_root).resolve()
    architecture = args.architecture
    private_root = validate_runtime_files(package_root)

    if repo_root_value := getattr(args, "repo_root", None):
        validate_release_payload(package_root, Path(repo_root_value).resolve())

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

    elf_paths = [path for path in package_root.rglob("*") if is_elf(path)]
    if not elf_paths:
        fail("Package contains no ELF objects")
    packaged_library_paths: dict[str, list[Path]] = {}
    for packaged_path in package_root.rglob("*"):
        if packaged_path.is_file():
            packaged_library_paths.setdefault(packaged_path.name, []).append(packaged_path)
    expected_machine = "Advanced Micro Devices X86-64" if architecture == "x86_64" else "AArch64"
    for path in elf_paths:
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
        needed = dynamic_values(path, "NEEDED")
        runpaths = dynamic_values(path, "RUNPATH")
        internal_search_directories = {
            (path.parent / entry.replace("$ORIGIN", str(path.parent))).resolve()
            for runpath in runpaths
            for entry in runpath.split(":")
            if entry
        }
        for library in needed:
            if library == "libfmt.so" or library.startswith("libfmt.so."):
                fail(f"{path} has forbidden dependency {library}")
            if (
                library not in packaged_library_paths
                and not library.startswith(SYSTEM_LIBRARY_PREFIXES)
            ):
                fail(f"{path} has unresolved or unclassified dependency {library}")
            if library in packaged_library_paths and not any(
                (directory / library).is_file() for directory in internal_search_directories
            ):
                fail(f"{path} cannot resolve packaged dependency {library} through its RUNPATH")

        relative = path.relative_to(package_root)
        if relative.parts[:2] == ("lib", "gstreamer-1.0"):
            expected_runpath = "$ORIGIN/../pek"
        elif relative.parts[:2] == ("lib", "pek") and len(relative.parts) == 3:
            expected_runpath = "$ORIGIN"
        else:
            expected_runpath = ""
        if expected_runpath and expected_runpath not in runpaths:
            fail(f"{path} has RUNPATH {runpaths}, expected {expected_runpath}")


def read_version(repo_root: Path) -> str:
    content = (repo_root / "development/meson.build").read_text(encoding="utf-8")
    match = re.search(r"project\([^)]*version:\s*'([^']+)'", content, re.DOTALL)
    if not match or not VERSION_PATTERN.fullmatch(match.group(1)):
        fail("development/meson.build must contain a stable MAJOR.MINOR.PATCH version")
    return match.group(1)


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
    if not args.build_label:
        changelog_section(repo_root, version)
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
            "commit": commit,
            "build_id": build_id,
            "x86_archive": f"pek-{build_id}-linux-x86_64.tar.gz",
            "arm_archive": f"pek-{build_id}-linux-aarch64.tar.gz",
            "docs_archive": f"pek-docs-{build_id}.tar.gz",
        }
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    stage_models_parser = subparsers.add_parser("stage-models")
    stage_models_parser.add_argument("--repo-root", default=".")
    stage_models_parser.add_argument("--stage-root", required=True)

    validate_package_parser = subparsers.add_parser("validate-package")
    validate_package_parser.add_argument("--architecture", choices=sorted(ARCHITECTURES), required=True)
    validate_package_parser.add_argument("--package-root", required=True)
    validate_package_parser.add_argument("--repo-root")

    prepare_parser = subparsers.add_parser("prepare")
    prepare_parser.add_argument("--repo-root", default=".")
    prepare_parser.add_argument("--commit", required=True)
    prepare_parser.add_argument("--build-label", default="")

    args = parser.parse_args()
    try:
        if args.command == "stage-models":
            stage_models(args)
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
