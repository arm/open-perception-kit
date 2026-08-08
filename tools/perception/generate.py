#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Generate the canonical checked-in Perception SDK snapshot."""

from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from release_common import sha256
from sdk_config import REPO_ROOT, SdkConfig, load_sdk_config

FLOWDATA_MANIFEST_FILENAME = "flowdata-manifest.json"
PERCEPTION_MANIFEST_FILENAME = "perception-sdk-manifest.json"
CPP_LICENSE_HEADER = """\
/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/
"""
PY_LICENSE_HEADER = """\
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
"""
CMAKE_LICENSE_HEADER = PY_LICENSE_HEADER
TS_LICENSE_HEADER = "// Copyright (C) 2026 Arm Limited. All rights reserved.\n"
CMAKE_FORMAT = "cmake-format"


def run(cmd: list[str]) -> None:
    subprocess.run(cmd, check=True)


def command_version(cmd: list[str]) -> str:
    result = subprocess.run(cmd, check=True, text=True, capture_output=True)
    return result.stdout.strip() or result.stderr.strip()


def git_commit(repository: Path) -> str:
    return command_version(["git", "-C", str(repository), "rev-parse", "HEAD"])


def require_perception_generator(config: SdkConfig) -> None:
    if not config.flowdata_generator.is_file():
        flowdata_path = config.flowdata_root.relative_to(REPO_ROOT).as_posix()
        raise RuntimeError(
            "Perception generator is missing. Initialize the submodule with:\n"
            f"  git submodule update --init --recursive {flowdata_path}"
        )


def generate_sdk(config: SdkConfig, generated_root: Path, flatc: str, python: str) -> None:
    require_perception_generator(config)
    common = [
        python, str(config.flowdata_generator), "generate",
        "--name", config.name,
        "--version", config.version,
        "--schema-dir", str(config.schema_dir),
        "--generated-root", str(generated_root),
        "--flatc", flatc,
    ]
    run([
        *common, "--sdk", "cpp", "--cpp-python-bridge", "--cmake", "--meson",
    ])
    run([*common, "--sdk", "python"])
    run([*common, "--sdk", "ts"])


def verify_flowdata_manifests(
    config: SdkConfig, generated_root: Path, python: str
) -> None:
    for sdk in ("cpp", "python", "ts"):
        command = [
            python, str(config.flowdata_generator), "verify-manifest",
            str(generated_root / sdk / FLOWDATA_MANIFEST_FILENAME),
            "--schema-root", str(config.schema_dir),
        ]
        run(command)


def read_flowdata_manifests(generated_root: Path) -> dict[str, object]:
    manifests: dict[str, object] = {}
    for sdk in ("cpp", "python", "ts"):
        path = generated_root / sdk / FLOWDATA_MANIFEST_FILENAME
        manifests[sdk] = json.loads(path.read_text(encoding="utf-8"))
        path.unlink()
    return manifests


def add_license_headers(generated_root: Path) -> None:
    for source in sorted((generated_root / "cpp").rglob("*")):
        if source.suffix in {".h", ".cpp"}:
            text = source.read_text(encoding="utf-8")
            if not text.startswith(CPP_LICENSE_HEADER):
                source.write_text(f"{CPP_LICENSE_HEADER}\n{text}", encoding="utf-8")
    modules = [
        *sorted((generated_root / "python").rglob("*.py")),
        *sorted((generated_root / "python").rglob("*.pyi")),
    ]
    for module in modules:
        text = module.read_text(encoding="utf-8")
        if not text.startswith(PY_LICENSE_HEADER):
            module.write_text(f"{PY_LICENSE_HEADER}\n{text}", encoding="utf-8")
    for integration in sorted((generated_root / "cpp" / "cmake").rglob("*.cmake")):
        text = integration.read_text(encoding="utf-8")
        if not text.startswith(CMAKE_LICENSE_HEADER):
            integration.write_text(f"{CMAKE_LICENSE_HEADER}{text}", encoding="utf-8")
    for module in sorted((generated_root / "ts").rglob("*.ts")):
        text = module.read_text(encoding="utf-8")
        if not text.startswith(TS_LICENSE_HEADER):
            module.write_text(f"{TS_LICENSE_HEADER}{text}", encoding="utf-8")


def prepare_typescript_package(config: SdkConfig, generated_root: Path) -> None:
    package_path = generated_root / "ts" / "package.json"
    package = json.loads(package_path.read_text(encoding="utf-8"))
    package["dependencies"] = {"flatbuffers": config.typescript_runtime.version}
    package["devDependencies"] = {"typescript": config.typescript_compiler.version}
    package["engines"] = {"node": f">={config.node_minimum_major}"}
    package["files"] = ["dist", "src"]
    package_path.write_text(
        json.dumps(package, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def build_typescript_package(
    config: SdkConfig,
    generated_root: Path,
    node: str,
    node_modules: Path,
) -> None:
    project = generated_root / "ts"
    transient_modules = project / "node_modules"
    required = {
        "flatbuffers": config.typescript_runtime.version,
        "typescript": config.typescript_compiler.version,
    }
    node_version = command_version([node, "--version"])
    match = __import__("re").fullmatch(r"v(\d+)\.\d+\.\d+", node_version)
    if match is None or int(match.group(1)) < config.node_minimum_major:
        raise RuntimeError(
            f"Node.js >={config.node_minimum_major} is required, got {node_version}"
        )
    for package_name, expected_version in required.items():
        package_path = node_modules / package_name / "package.json"
        if not package_path.is_file():
            raise RuntimeError(
                f"missing {package_name} in TypeScript modules directory: {node_modules}"
            )
        actual_version = json.loads(package_path.read_text(encoding="utf-8")).get("version")
        if actual_version != expected_version:
            raise RuntimeError(
                f"{package_name} version mismatch: expected {expected_version}, got {actual_version}"
            )
    transient_modules.mkdir()
    try:
        for package_name in required:
            (transient_modules / package_name).symlink_to(
                node_modules / package_name, target_is_directory=True
            )
        run([
            node,
            str(node_modules / "typescript" / "bin" / "tsc"),
            "-p",
            str(project / "tsconfig.json"),
        ])
    finally:
        shutil.rmtree(transient_modules)


def format_cpp_sources(generated_root: Path, clang_format: str) -> None:
    sources = sorted(
        path for path in (generated_root / "cpp").rglob("*")
        if path.suffix in {".h", ".cpp"}
    )
    if not shutil.which(clang_format):
        raise RuntimeError(f"{clang_format} is required to format generated C++ files")
    run([clang_format, f"--style=file:{REPO_ROOT / '.clang-format'}", "-i", *map(str, sources)])


def autopep8_python() -> str:
    if "PEK_DEVTOOLS_VENV" in os.environ:
        candidate = Path(os.environ["PEK_DEVTOOLS_VENV"]) / "bin" / "python"
        if candidate.exists():
            return str(candidate)
    return sys.executable


def format_python_modules(generated_root: Path, python: str) -> None:
    modules = [
        *sorted((generated_root / "python").rglob("*.py")),
        *sorted((generated_root / "python").rglob("*.pyi")),
    ]
    try:
        subprocess.run(
            [python, "-m", "autopep8", "--version"], check=True,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
    except (FileNotFoundError, subprocess.CalledProcessError) as exc:
        raise RuntimeError("autopep8 is required to format generated Python files") from exc
    run([python, "-m", "autopep8", "--in-place", *map(str, modules)])


def format_cmake_integrations(generated_root: Path) -> None:
    integrations = sorted((generated_root / "cpp" / "cmake").rglob("*.cmake"))
    if not shutil.which(CMAKE_FORMAT):
        raise RuntimeError(f"{CMAKE_FORMAT} is required to format generated CMake files")
    run([
        CMAKE_FORMAT,
        "-c",
        str(REPO_ROOT / ".cmake-format.yaml"),
        "-i",
        *map(str, integrations),
    ])


def validate_flowdata_manifests(
    config: SdkConfig, manifests: dict[str, object]
) -> None:
    cpp = manifests.get("cpp")
    python_manifest = manifests.get("python")
    typescript_manifest = manifests.get("ts")
    if not all(isinstance(value, dict) for value in (cpp, python_manifest, typescript_manifest)):
        raise RuntimeError("flowdata generation did not produce C++, Python, and TypeScript manifests")
    shared_fields = (
        "sdk", "generator", "flatc", "schema_files", "schema_set_sha256", "payloads",
    )
    for field in shared_fields:
        if not all(cpp.get(field) == manifest.get(field) for manifest in (python_manifest, typescript_manifest)):
            raise RuntimeError(f"flowdata manifests disagree on {field}")
    expected_sdk = {"name": config.name, "version": config.version}
    if cpp.get("sdk") != expected_sdk:
        raise RuntimeError("flowdata manifest SDK identity does not match sdk.json")
    if cpp.get("flatc", {}).get("semantic_version") != config.flatbuffers_version:
        raise RuntimeError(
            "flowdata compiler version does not match the exact FlatBuffers lock in sdk.json"
        )
    cpp_outputs = cpp.get("outputs")
    python_outputs = python_manifest.get("outputs")
    if cpp_outputs != {
        "cpp_python_bridge": True,
        "integrations": ["cmake", "meson"],
        "sdk": "cpp",
    }:
        raise RuntimeError("flowdata C++ outputs do not match the Perception SDK contract")
    if python_outputs != {
        "cpp_python_bridge": False,
        "integrations": [],
        "sdk": "python",
    }:
        raise RuntimeError("flowdata Python outputs do not match the Perception SDK contract")
    if typescript_manifest.get("outputs") != {
        "cpp_python_bridge": False,
        "integrations": [],
        "sdk": "ts",
    }:
        raise RuntimeError("flowdata TypeScript outputs do not match the Perception SDK contract")


def normalize_integration_files(config: SdkConfig, generated_root: Path) -> None:
    paths = (
        generated_root / "cpp" / "cmake" / f"{config.name}.cmake",
        generated_root / "cpp" / "meson" / config.name / "meson.build",
        generated_root / "cpp" / "meson" / config.name / "python_bridge" / "meson.build",
    )
    for path in paths:
        path.write_text(path.read_text(encoding="utf-8").rstrip() + "\n", encoding="utf-8")


def write_internal_meson(config: SdkConfig, generated_root: Path, target: Path) -> None:
    cpp_root = os.path.relpath(config.generated_root / "cpp", config.internal_meson_path.parent)
    content = (
        "# Generated project adapter. Do not edit.\n"
        "# Regenerate with ./scripts/perception-sdk.sh generate.\n\n"
        f"{config.name}_version = '{config.version}'\n"
        f"{config.name}_flatbuffers_version_requirement = '=={config.flatbuffers_version}'\n\n"
        f"_{config.name}_flatbuffers_dep = dependency(\n"
        "  'flatbuffers',\n"
        "  required : true,\n"
        f"  version : {config.name}_flatbuffers_version_requirement,\n"
        ")\n"
        f"_{config.name}_inc = include_directories('{cpp_root}')\n\n"
        f"{config.name}_dep = declare_dependency(\n"
        f"  include_directories : [_{config.name}_inc],\n"
        "  compile_args : ['-std=c++20'],\n"
        f"  dependencies : [_{config.name}_flatbuffers_dep],\n"
        ")\n"
        "\n"
        "if get_option('tests')\n"
        f"  _{config.name}_python = import('python').find_installation()\n"
        f"  _{config.name}_python_embed_dep = _{config.name}_python.dependency(\n"
        "    embed : true,\n"
        "    required : true,\n"
        "  )\n"
        f"  _{config.name}_python_bridge_sources = files(\n"
        f"    '{cpp_root}/python_bridge/{config.name}_python_bridge.cpp',\n"
        "  )\n\n"
        f"  {config.name}_python_bridge_dep = declare_dependency(\n"
        f"    include_directories : [_{config.name}_inc],\n"
        "    compile_args : ['-std=c++20'],\n"
        f"    dependencies : [{config.name}_dep, _{config.name}_python_embed_dep],\n"
        f"    sources : _{config.name}_python_bridge_sources,\n"
        "  )\n"
        "endif\n"
    )
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content, encoding="utf-8")


def is_transient_generated_path(path: Path, root: Path) -> bool:
    relative = path.relative_to(root)
    return (
        "__pycache__" in relative.parts
        or any(part.endswith(".egg-info") for part in relative.parts)
        or relative.parts[:2] == ("python", "build")
        or "node_modules" in relative.parts
        or path.suffix == ".pyc"
    )


def _file_records(root: Path, excluded: set[Path] | None = None) -> list[dict[str, object]]:
    excluded = excluded or set()
    return [
        {"path": path.relative_to(root).as_posix(), "sha256": sha256(path), "size": path.stat().st_size}
        for path in sorted(candidate for candidate in root.rglob("*") if candidate.is_file())
        if path not in excluded and not is_transient_generated_path(path, root)
    ]


def _schema_records(root: Path) -> list[dict[str, object]]:
    return [
        {"path": path.relative_to(root).as_posix(), "sha256": sha256(path), "size": path.stat().st_size}
        for path in sorted(root.rglob("*.fbs")) if path.is_file()
    ]


def _schema_set_sha256(root: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(root.rglob("*.fbs")):
        relative = path.relative_to(root).as_posix().encode("utf-8")
        content = path.read_bytes()
        digest.update(len(relative).to_bytes(8, "big"))
        digest.update(relative)
        digest.update(len(content).to_bytes(8, "big"))
        digest.update(content)
    return digest.hexdigest()


def _generation_tool_records() -> list[dict[str, object]]:
    return [
        {
            "path": path.relative_to(REPO_ROOT).as_posix(),
            "sha256": sha256(path),
            "size": path.stat().st_size,
        }
        for path in (
            Path(__file__).resolve(),
            Path(__file__).with_name("release_common.py").resolve(),
            Path(__file__).with_name("sdk_config.py").resolve(),
            REPO_ROOT / ".clang-format",
            REPO_ROOT / ".cmake-format.yaml",
        )
    ]


def write_perception_manifest(
    config: SdkConfig,
    generated_root: Path,
    internal_meson: Path,
    flowdata_manifests: dict[str, object],
    clang_format: str,
    formatter_python: str,
    node: str,
) -> None:
    manifest_path = generated_root / PERCEPTION_MANIFEST_FILENAME
    manifest = {
        "artifact": {"name": f"{config.name}-sdk", "version": config.version},
        "descriptor": {
            "path": config.descriptor_path.relative_to(REPO_ROOT).as_posix(),
            "sha256": config.descriptor_sha256,
        },
        "files": _file_records(generated_root, {manifest_path}),
        "generation": {
            "flowdata_sdk": {
                "commit": git_commit(config.flowdata_root),
                "generator": flowdata_manifests["cpp"]["generator"],
            },
            "tools": _generation_tool_records(),
        },
        "upstream_receipts": flowdata_manifests,
        "postprocessing": {
            "copyright_headers": "Arm Limited 2025",
            "cmake_formatter": command_version([CMAKE_FORMAT, "--version"]),
            "cpp_formatter": command_version([clang_format, "--version"]),
            "python_formatter": command_version([formatter_python, "-m", "autopep8", "--version"]),
            "typescript": {
                "compiler": config.typescript_compiler.version,
                "flatbuffers_runtime": config.typescript_runtime.version,
                "node": f">={config.node_minimum_major}",
            },
        },
        "project_files": [{
            "path": config.internal_meson_path.relative_to(REPO_ROOT).as_posix(),
            "sha256": sha256(internal_meson),
            "size": internal_meson.stat().st_size,
        }],
    }
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def verify_perception_manifest(
    config: SdkConfig,
    generated_root: Path | None = None,
    internal_meson: Path | None = None,
) -> dict[str, object]:
    generated_root = generated_root or config.generated_root
    internal_meson = internal_meson or config.internal_meson_path
    path = generated_root / PERCEPTION_MANIFEST_FILENAME
    manifest = json.loads(path.read_text(encoding="utf-8"))
    expected_fields = {
        "artifact", "descriptor", "files", "generation", "postprocessing",
        "project_files", "upstream_receipts",
    }
    if set(manifest) != expected_fields:
        raise RuntimeError("Perception SDK manifest fields are stale")
    if manifest.get("artifact") != {"name": f"{config.name}-sdk", "version": config.version}:
        raise RuntimeError("Perception SDK manifest identity does not match sdk.json")
    expected_descriptor = {
        "path": config.descriptor_path.relative_to(REPO_ROOT).as_posix(),
        "sha256": config.descriptor_sha256,
    }
    if manifest.get("descriptor") != expected_descriptor:
        raise RuntimeError("Perception SDK manifest descriptor identity does not match sdk.json")
    if manifest.get("files") != _file_records(generated_root, {path}):
        raise RuntimeError("Perception SDK manifest file list or hashes are stale")
    expected_project = [{
        "path": config.internal_meson_path.relative_to(REPO_ROOT).as_posix(),
        "sha256": sha256(internal_meson),
        "size": internal_meson.stat().st_size,
    }]
    if manifest.get("project_files") != expected_project:
        raise RuntimeError("Perception SDK project integration is stale")
    generation = manifest.get("generation")
    if not isinstance(generation, dict):
        raise RuntimeError("Perception SDK generation identity is missing")
    if generation.get("tools") != _generation_tool_records():
        raise RuntimeError("Perception SDK generation tools changed; regenerate the SDK")
    flowdata_identity = generation.get("flowdata_sdk")
    if not isinstance(flowdata_identity, dict):
        raise RuntimeError("Perception SDK flowdata identity is missing")
    if flowdata_identity.get("commit") != git_commit(config.flowdata_root):
        raise RuntimeError("flowdata-sdk changed; regenerate the Perception SDK")
    flowdata = manifest.get("upstream_receipts")
    if not isinstance(flowdata, dict) or set(flowdata) != {"cpp", "python", "ts"}:
        raise RuntimeError("Perception SDK manifest has incomplete flowdata metadata")
    for sdk in ("cpp", "python", "ts"):
        sdk_manifest = flowdata[sdk]
        if sdk_manifest.get("sdk", {}).get("name") != config.name:
            raise RuntimeError(f"{sdk} manifest SDK name does not match sdk.json")
        if sdk_manifest.get("sdk", {}).get("version") != config.version:
            raise RuntimeError(f"{sdk} manifest SDK version does not match sdk.json")
        if sdk_manifest.get("generator") != flowdata_identity.get("generator"):
            raise RuntimeError(f"{sdk} generator identity is stale")
        if sdk_manifest.get("schema_files") != _schema_records(config.schema_dir):
            raise RuntimeError(f"{sdk} schema inputs are stale")
        if sdk_manifest.get("schema_set_sha256") != _schema_set_sha256(config.schema_dir):
            raise RuntimeError(f"{sdk} schema-set digest is stale")
    return manifest


def prepare_sdk(
    config: SdkConfig,
    generated_root: Path,
    internal_meson: Path,
    clang_format: str,
    formatter_python: str,
    python: str,
    node: str,
    node_modules: Path,
) -> None:
    verify_flowdata_manifests(config, generated_root, python)
    flowdata_manifests = read_flowdata_manifests(generated_root)
    add_license_headers(generated_root)
    prepare_typescript_package(config, generated_root)
    format_cpp_sources(generated_root, clang_format)
    format_python_modules(generated_root, formatter_python)
    build_typescript_package(config, generated_root, node, node_modules)
    validate_flowdata_manifests(config, flowdata_manifests)
    normalize_integration_files(config, generated_root)
    format_cmake_integrations(generated_root)
    write_internal_meson(config, generated_root, internal_meson)
    write_perception_manifest(
        config, generated_root, internal_meson, flowdata_manifests,
        clang_format, formatter_python, node,
    )
    verify_perception_manifest(config, generated_root, internal_meson)


def _tree_diff(expected: Path, actual: Path) -> str:
    expected_files = {
        path.relative_to(expected) for path in expected.rglob("*")
        if path.is_file() and not is_transient_generated_path(path, expected)
    }
    actual_files = {
        path.relative_to(actual) for path in actual.rglob("*")
        if path.is_file() and not is_transient_generated_path(path, actual)
    }
    lines: list[str] = []
    for relative in sorted(expected_files | actual_files):
        left, right = expected / relative, actual / relative
        if not left.exists():
            lines.append(f"only in checked-in SDK: {relative}")
        elif not right.exists():
            lines.append(f"missing from checked-in SDK: {relative}")
        elif left.read_bytes() != right.read_bytes():
            if left.suffix in {".json", ".py", ".pyi", ".h", ".cpp", ".txt", ".md"} or left.name in {"meson.build", "pyproject.toml"}:
                lines.extend(difflib.unified_diff(
                    right.read_text(encoding="utf-8").splitlines(),
                    left.read_text(encoding="utf-8").splitlines(),
                    fromfile=f"checked-in/{relative}", tofile=f"generated/{relative}", lineterm="",
                ))
            else:
                lines.append(f"content differs: {relative}")
    return "\n".join(lines)


def generate_candidate(
    config: SdkConfig,
    workspace: Path,
    flatc: str,
    python: str,
    clang_format: str,
    formatter_python: str,
    node: str,
    node_modules: Path,
) -> tuple[Path, Path]:
    generated = workspace / "generated"
    internal = workspace / "development" / "perception" / "meson.build"
    generate_sdk(config, generated, flatc, python)
    prepare_sdk(
        config, generated, internal, clang_format, formatter_python, python,
        node, node_modules,
    )
    return generated, internal


def check_generated(
    config: SdkConfig,
    flatc: str,
    python: str,
    clang_format: str,
    formatter_python: str,
    node: str,
    node_modules: Path,
) -> bool:
    with tempfile.TemporaryDirectory(prefix=".perception-check-", dir=REPO_ROOT) as tmp:
        candidate, internal = generate_candidate(
            config, Path(tmp), flatc, python, clang_format, formatter_python,
            node, node_modules,
        )
        differences = _tree_diff(candidate, config.generated_root)
        if (
            not config.internal_meson_path.is_file()
            or internal.read_bytes() != config.internal_meson_path.read_bytes()
        ):
            differences += "\ninternal Meson integration differs"
        if differences.strip():
            print(differences.strip())
            print("Perception generated SDKs are stale. Run ./scripts/perception-sdk.sh generate in the container.")
            return False
    verify_perception_manifest(config)
    return True


def install_candidate(config: SdkConfig, candidate: Path, internal: Path, workspace: Path) -> None:
    root_backup = workspace / "generated.backup"
    meson_backup = workspace / "meson.build.backup"
    installed_root = False
    installed_meson = False
    try:
        if config.generated_root.exists():
            os.replace(config.generated_root, root_backup)
        config.generated_root.parent.mkdir(parents=True, exist_ok=True)
        os.replace(candidate, config.generated_root)
        installed_root = True
        if config.internal_meson_path.exists():
            os.replace(config.internal_meson_path, meson_backup)
        config.internal_meson_path.parent.mkdir(parents=True, exist_ok=True)
        os.replace(internal, config.internal_meson_path)
        installed_meson = True
    except Exception:
        if installed_meson and config.internal_meson_path.exists():
            config.internal_meson_path.unlink()
        if meson_backup.exists():
            os.replace(meson_backup, config.internal_meson_path)
        if installed_root and config.generated_root.exists():
            shutil.rmtree(config.generated_root)
        if root_backup.exists():
            os.replace(root_backup, config.generated_root)
        raise
    if root_backup.exists():
        shutil.rmtree(root_backup)
    if meson_backup.exists():
        meson_backup.unlink()


class SdkHelpFormatter(
    argparse.ArgumentDefaultsHelpFormatter,
    argparse.RawDescriptionHelpFormatter,
):
    def _get_help_string(self, action: argparse.Action) -> str:
        if action.default in {None, False, argparse.SUPPRESS} or action.required:
            return action.help
        return super()._get_help_string(action)


def parse_args() -> argparse.Namespace:
    check_mode = "--check" in sys.argv[1:]
    command = "check" if check_mode else "generate"
    description = (
        "Regenerate the SDK in a temporary directory and fail if the checked-in "
        "snapshot differs."
        if check_mode
        else __doc__
    )
    parser = argparse.ArgumentParser(
        prog=f"./scripts/perception-sdk.sh {command}",
        description=description,
        formatter_class=SdkHelpFormatter,
        epilog=(
            "examples:\n"
            f"  ./scripts/perception-sdk.sh {command}\n"
            f"  ./scripts/perception-sdk.sh {command} --flatc /usr/local/bin/flatc"
        ),
    )
    parser.add_argument("--check", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument(
        "--flatc",
        default="flatc",
        metavar="EXECUTABLE",
        help="FlatBuffers compiler used by flowdata-sdk generation",
    )
    parser.add_argument(
        "--python",
        default=sys.executable,
        metavar="EXECUTABLE",
        help="Python interpreter used to run flowdata-sdk and manifest verification",
    )
    parser.add_argument(
        "--clang-format",
        default="clang-format",
        metavar="EXECUTABLE",
        help="clang-format executable used to format generated C++ sources",
    )
    parser.add_argument(
        "--formatter-python",
        default=autopep8_python(),
        metavar="EXECUTABLE",
        help="Python interpreter whose autopep8 module formats generated Python files",
    )
    parser.add_argument(
        "--node",
        default="node",
        metavar="EXECUTABLE",
        help="Node.js executable used to compile the generated TypeScript SDK",
    )
    parser.add_argument(
        "--node-modules",
        type=Path,
        default=None,
        metavar="PATH",
        help="directory containing the locked flatbuffers and typescript npm packages",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        config = load_sdk_config()
        node_modules = args.node_modules
        if node_modules is None:
            node_modules = Path(command_version(["npm", "root", "--global"]))
        if args.check:
            return 0 if check_generated(
                config, args.flatc, args.python, args.clang_format, args.formatter_python,
                args.node, node_modules,
            ) else 1
        with tempfile.TemporaryDirectory(prefix=".perception-generate-", dir=REPO_ROOT) as tmp:
            workspace = Path(tmp)
            generated, internal = generate_candidate(
                config, workspace, args.flatc, args.python,
                args.clang_format, args.formatter_python, args.node, node_modules,
            )
            install_candidate(config, generated, internal, workspace)
        verify_perception_manifest(config)
        print(f"Generated {config.name} SDK {config.version} in {config.generated_root}")
        return 0
    except (OSError, RuntimeError, subprocess.CalledProcessError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
