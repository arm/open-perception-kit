#!/usr/bin/env python3
################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################

"""Generate the canonical checked-in open-perception-kit snapshot."""

from __future__ import annotations

import argparse
import ast
import difflib
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from release_common import sha256
from sdk_config import (
    PRODUCT_VERSION,
    PRODUCT_VERSION_PATH,
    REPO_ROOT,
    SDK_CONFIG_PATH,
    SEMVER,
    SdkConfig,
    load_sdk_config,
)

FLOWDATA_MANIFEST_FILENAME = "flowdata-manifest.json"
PERCEPTION_MANIFEST_FILENAME = "open-perception-kit-manifest.json"
SDK_LICENSE = "Apache-2.0"
SDK_LEGAL_FILES = ("LICENSE", "NOTICE")
# Authored inputs copied into each generated language package.
SDK_LEGAL_INPUT_DIR = Path(__file__).with_name("generator-inputs")
CPP_LICENSE_HEADER = """\
/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 *************************************************************/
"""
PY_LICENSE_HEADER = """\
################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################
"""
CMAKE_LICENSE_HEADER = PY_LICENSE_HEADER
TS_LICENSE_HEADER = "// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates\n"
RUST_LICENSE_HEADER = TS_LICENSE_HEADER
TS_GENERATED_HEADER = """\
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.
"""
CMAKE_FORMAT = "cmake-format"
RUSTFMT = "rustfmt"
MESON_BUILD_FILENAME = "meson.build"
RUST_FIXTURE = REPO_ROOT / "tools/perception/tests/fixtures/opk-box-detections-v0.2.1.hex"
RUST_FIXTURE_SDK_VERSION = "0.2.1"
RUST_FIXTURE_SCHEMA_SET_SHA256 = "0ba6dfe959e1453ce12c7a8707623bc15d94d52c9235c26f7e27f31dda0775c5"
PLUMBER_PROJECT = REPO_ROOT / "tools/plumber/pyproject.toml"
WEB_BUILD = REPO_ROOT / "development/web/build.mjs"


def run(cmd: list[str]) -> None:
    subprocess.run(cmd, check=True)


def set_product_version(path: Path, version: str) -> None:
    if SEMVER.fullmatch(version) is None:
        raise RuntimeError("product version must use MAJOR.MINOR.PATCH form")
    text = path.read_text(encoding="utf-8")
    matches = list(PRODUCT_VERSION.finditer(text))
    if len(matches) != 1:
        raise RuntimeError("product Meson file must contain exactly one version")
    start, end = matches[0].span(1)
    path.write_text(f"{text[:start]}{version}{text[end:]}", encoding="utf-8")


def set_package_prerelease(path: Path, enabled: bool) -> None:
    descriptor = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(descriptor.get("package_prerelease"), bool):
        raise RuntimeError("package_prerelease must be boolean")
    if descriptor["package_prerelease"] != enabled:
        descriptor["package_prerelease"] = enabled
        path.write_text(
            json.dumps(descriptor, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )


def command_version(cmd: list[str]) -> str:
    result = subprocess.run(cmd, check=True, text=True, capture_output=True)
    return result.stdout.strip() or result.stderr.strip()


def rustfmt_version() -> str:
    version = command_version([RUSTFMT, "--version"])
    match = re.match(r"rustfmt (\d+\.\d+\.\d+)", version)
    if match is None:
        raise RuntimeError(f"Unable to parse rustfmt version: {version}")
    return f"rustfmt {match.group(1)}"


def _validate_flowdata_location(root: Path, generator: Path) -> None:
    if (
        not root.is_relative_to(REPO_ROOT)
        or not generator.is_relative_to(root)
        or ".." in generator.parts
        or ".." in root.parts
    ):
        raise RuntimeError("flowdata-sdk sources must stay inside the repository")
    for path in (generator, *generator.parents):
        if path == REPO_ROOT:
            break
        if path.is_symlink():
            raise RuntimeError(f"flowdata-sdk sources must not contain symlinks: {path}")


def _flowdata_directory_entries(directory: Path) -> tuple[list[Path], list[Path]]:
    directories: list[Path] = []
    sources: list[Path] = []
    for path in directory.iterdir():
        if path.is_symlink():
            raise RuntimeError(f"flowdata-sdk sources must not contain symlinks: {path}")
        if path.name == "__pycache__":
            continue
        if path.is_dir():
            directories.append(path)
        elif path.suffix == ".py":
            if not path.is_file():
                raise RuntimeError(f"flowdata-sdk source is not a regular file: {path}")
            sources.append(path)
    return directories, sources


def _flowdata_source_version(version_path: Path) -> str:
    # Read the constant without importing or executing the generator during verification.
    try:
        statements = ast.parse(version_path.read_text(encoding="utf-8")).body
    except SyntaxError as exc:
        raise RuntimeError("flowdata-sdk engine/version.py is invalid") from exc
    if (
        len(statements) != 1
        or not isinstance(statements[0], ast.Assign)
        or len(statements[0].targets) != 1
        or not isinstance(statements[0].targets[0], ast.Name)
        or statements[0].targets[0].id != "GENERATOR_VERSION"
        or not isinstance(statements[0].value, ast.Constant)
        or not isinstance(statements[0].value.value, str)
        or not SEMVER.fullmatch(statements[0].value.value)
    ):
        raise RuntimeError("flowdata-sdk engine/version.py must define a literal GENERATOR_VERSION")
    return statements[0].value.value


def flowdata_source_identity(config: SdkConfig) -> dict[str, object]:
    root = config.flowdata_root
    generator = config.flowdata_generator
    _validate_flowdata_location(root, generator)
    tree = generator.parent
    if not tree.is_dir():
        raise RuntimeError(f"tracked flowdata-sdk generator sources are missing: {tree}")
    sources: list[Path] = []
    directories = [tree]
    while directories:
        children, files = _flowdata_directory_entries(directories.pop())
        directories.extend(children)
        sources.extend(files)
    essential = (generator, tree / "engine/app.py", tree / "engine/__init__.py",
                 tree / "engine/version.py", tree / "engine/manifest.py")
    for path in essential:
        if path not in sources:
            raise RuntimeError(f"tracked flowdata-sdk generator source is missing: {path}")
    return {
        "generator": {"name": "flowdata-sdk", "version": _flowdata_source_version(tree / "engine/version.py")},
        "sources": [
            {"path": path.relative_to(root).as_posix(), "sha256": sha256(path),
             "size": path.stat().st_size}
            for path in sorted(sources)
        ],
    }


def generate_sdk(config: SdkConfig, generated_root: Path, flatc: str, python: str) -> None:
    flowdata_source_identity(config)
    common = [
        python, str(config.flowdata_generator), "generate",
        "--name", config.public_name,
        "--version", config.version,
        "--schema-dir", str(config.schema_dir),
        "--generated-root", str(generated_root),
        "--flatc", flatc,
    ]
    run([
        *common, "--sdk", "cpp", "--cpp-python-bridge", "--cmake", "--meson",
    ])
    run([*common, "--sdk", "python"])
    run([*common, "--sdk", "rust"])
    run([*common, "--sdk", "ts"])


def prepare_python_package(
    python_project: Path,
    source_name: str,
    source_version: str,
    package_version: str,
) -> None:
    pyproject = python_project / "pyproject.toml"
    text = pyproject.read_text(encoding="utf-8")
    source = (
        f'[project]\nname = "{source_name}"\nversion = "{source_version}"\n'
    )
    target = (
        f'[project]\nname = "{source_name}"\n'
        f'version = "{package_version}"\n'
        f'license = "{SDK_LICENSE}"\n'
        'license-files = ["LICENSE", "NOTICE"]\n'
    )
    if text.count(source) != 1:
        raise RuntimeError("generated Python project name is unexpected")
    pyproject.write_text(text.replace(source, target), encoding="utf-8")


def synchronize_plumber_dependency(
    path: Path, distribution_name: str, version: str, check: bool
) -> bool:
    text = path.read_text(encoding="utf-8")
    expected, count = re.subn(
        rf'(?m)^(\s*"{re.escape(distribution_name)}==)[^"]+(",)$',
        rf"\g<1>{version}\g<2>",
        text,
    )
    if count != 1:
        raise RuntimeError("plumber must declare one exact open-perception-kit dependency")
    if expected == text:
        return True
    if check:
        print("tools/plumber/pyproject.toml open-perception-kit dependency is stale")
        return False
    path.write_text(expected, encoding="utf-8")
    return True


def synchronize_project_consumers(config: SdkConfig, node: str, check: bool) -> bool:
    plumber_current = synchronize_plumber_dependency(
        PLUMBER_PROJECT,
        config.public_name,
        config.python_package_version,
        check,
    )
    run([node, str(WEB_BUILD), "check" if check else "generate"])
    return plumber_current


def verify_flowdata_manifests(
    config: SdkConfig, generated_root: Path, python: str
) -> None:
    for sdk in ("cpp", "python", "rust", "ts"):
        command = [
            python, str(config.flowdata_generator), "verify-manifest",
            str(generated_root / sdk / FLOWDATA_MANIFEST_FILENAME),
            "--schema-root", str(config.schema_dir),
        ]
        run(command)


def read_flowdata_manifests(generated_root: Path) -> dict[str, object]:
    manifests: dict[str, object] = {}
    for sdk in ("cpp", "python", "rust", "ts"):
        path = generated_root / sdk / FLOWDATA_MANIFEST_FILENAME
        manifests[sdk] = json.loads(path.read_text(encoding="utf-8"))
        path.unlink()
    return manifests


def _add_license_header(path: Path, marker: str, prefix: str) -> None:
    text = path.read_text(encoding="utf-8")
    if not text.startswith(marker):
        path.write_text(f"{prefix}{text}", encoding="utf-8")


def _add_license_headers_to(paths: list[Path], marker: str, prefix: str) -> None:
    for path in paths:
        _add_license_header(path, marker, prefix)


def add_license_headers(generated_root: Path) -> None:
    cpp_sources = [
        source
        for source in sorted((generated_root / "cpp").rglob("*"))
        if source.suffix in {".h", ".cpp"}
    ]
    _add_license_headers_to(
        cpp_sources, CPP_LICENSE_HEADER, f"{CPP_LICENSE_HEADER}\n"
    )
    modules = [
        *sorted((generated_root / "python").rglob("*.py")),
        *sorted((generated_root / "python").rglob("*.pyi")),
    ]
    _add_license_headers_to(modules, PY_LICENSE_HEADER, f"{PY_LICENSE_HEADER}\n")
    _add_license_headers_to(
        sorted((generated_root / "cpp" / "cmake").rglob("*.cmake")),
        CMAKE_LICENSE_HEADER,
        CMAKE_LICENSE_HEADER,
    )
    _add_license_headers_to(
        sorted((generated_root / "ts").rglob("*.ts")),
        TS_LICENSE_HEADER,
        TS_LICENSE_HEADER,
    )
    _add_license_headers_to(
        sorted((generated_root / "rust").rglob("*.rs")),
        RUST_LICENSE_HEADER,
        RUST_LICENSE_HEADER,
    )
    _add_license_headers_to(
        [*sorted(generated_root.rglob("*.toml")),
         *sorted(generated_root.rglob(MESON_BUILD_FILENAME))],
        PY_LICENSE_HEADER,
        PY_LICENSE_HEADER,
    )


def copy_sdk_legal_files(generated_root: Path) -> None:
    for language in ("cpp", "python", "rust", "ts"):
        for name in SDK_LEGAL_FILES:
            shutil.copyfile(SDK_LEGAL_INPUT_DIR / name, generated_root / language / name)


def prepare_typescript_package(config: SdkConfig, generated_root: Path) -> None:
    package_path = generated_root / "ts" / "package.json"
    package = json.loads(package_path.read_text(encoding="utf-8"))
    package["dependencies"] = {"flatbuffers": config.typescript_runtime.version}
    package["devDependencies"] = {"typescript": config.typescript_compiler.version}
    package["engines"] = {"node": f">={config.node_minimum_major}"}
    package["files"] = ["dist", "src", *SDK_LEGAL_FILES]
    package["license"] = SDK_LICENSE
    package["version"] = config.cargo_package_version
    package_path.write_text(
        json.dumps(package, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def prepare_rust_tests(config: SdkConfig, generated_root: Path) -> None:
    rust_root = generated_root / "rust"
    cargo_toml = rust_root / "Cargo.toml"
    expected_name = f'name = "{config.public_name}"'
    cargo = cargo_toml.read_text(encoding="utf-8")
    if expected_name not in cargo:
        raise RuntimeError("generated Rust package name does not match sdk.json")
    source_version = f'version = "{config.version}"'
    if cargo.count(source_version) != 1:
        raise RuntimeError("generated Rust package version is unexpected")
    cargo_toml.write_text(
        cargo.replace(
            source_version,
            f'version = "{config.cargo_package_version}"\nlicense = "{SDK_LICENSE}"',
        ),
        encoding="utf-8",
    )
    fixture_target = rust_root / "tests" / "fixtures" / RUST_FIXTURE.name
    fixture_target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(RUST_FIXTURE, fixture_target)
    crate_name = config.public_name
    test_source = f'''\
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.

use {crate_name}::fb::open_perception_kit::metadata::BoxDetectionsT;
use {crate_name}::{{
    external_key, payload, EntryRef, Envelope, ProducerIdentityStatus, {config.public_name.upper()}_NAME,
    {config.public_name.upper()}_VERSION, SCHEMA_SET_SHA256,
}};

const FIXTURE_SDK_NAME: &str = "perception";
const FIXTURE_SDK_VERSION: &str = "{RUST_FIXTURE_SDK_VERSION}";
const FIXTURE_SCHEMA_SET_SHA256: &str = "{RUST_FIXTURE_SCHEMA_SET_SHA256}";

fn decode_hex(value: &str) -> Vec<u8> {{
    let compact = value.trim();
    (0..compact.len())
        .step_by(2)
        .map(|index| u8::from_str_radix(&compact[index..index + 2], 16).unwrap())
        .collect()
}}

fn assert_send_sync_static<T: Send + Sync + 'static>() {{}}
fn assert_send<T: Send>(_: T) {{}}

#[test]
fn decodes_python_produced_box_detections_packet() {{
    assert_send_sync_static::<Envelope>();
    assert_send_sync_static::<BoxDetectionsT>();
    let packet = decode_hex(include_str!("fixtures/{RUST_FIXTURE.name}"));
    let envelope = Envelope::decode(packet).expect("pinned OPK packet must decode");

    assert_eq!(envelope.producer_sdk_name(), FIXTURE_SDK_NAME);
    assert_eq!(envelope.producer_sdk_version(), FIXTURE_SDK_VERSION);
    assert_eq!(
        envelope.producer_schema_set_sha256(),
        FIXTURE_SCHEMA_SET_SHA256
    );
    let expected_identity = if {config.public_name.upper()}_NAME != FIXTURE_SDK_NAME {{
        ProducerIdentityStatus::SdkNameMismatch
    }} else if {config.public_name.upper()}_VERSION != FIXTURE_SDK_VERSION {{
        ProducerIdentityStatus::SdkVersionMismatch
    }} else if SCHEMA_SET_SHA256 != FIXTURE_SCHEMA_SET_SHA256 {{
        ProducerIdentityStatus::SchemaSetMismatch
    }} else {{
        ProducerIdentityStatus::ExactMatch
    }};
    assert_eq!(envelope.producer_identity(), expected_identity);
    assert_eq!(envelope.len(), 2);

    let box_detections = payload::<BoxDetectionsT>();
    let preserved_payload = if expected_identity == ProducerIdentityStatus::ExactMatch {{
        let boxes = envelope.get(box_detections, 0).expect("BoxDetections payload");
        let layer = boxes.layer.as_ref().expect("layer");
        assert_eq!(layer.engine.as_deref(), Some("fixture"));
        assert_eq!(layer.model.as_deref(), Some("yolov11n"));
        assert_eq!(layer.infer_element_id.as_deref(), Some("infer0"));
        let detections = boxes.detections.as_ref().expect("detections");
        assert_eq!(detections.len(), 1);
        let detection = &detections[0];
        assert_eq!(detection.class_id, 3);
        assert_eq!(detection.text.as_deref(), Some("car"));
        assert_eq!(detection.confidence, 0.875);
        let object = detection.object.as_ref().expect("object");
        assert_eq!((object.id, object.parent_id, object.creation_ts_ns), (42, 7, 123_456_789));
        let rectangle = detection.box_.as_ref().expect("box");
        assert_eq!((rectangle.x, rectangle.y, rectangle.width, rectangle.height),
            (10.5, 20.25, 30.75, 40.5));
        None
    }} else {{
        assert!(envelope.get(box_detections, 0).is_none());
        match envelope.entries().next() {{
            Some(EntryRef::Unknown {{ id, bytes }}) => Some((id, bytes.to_vec())),
            entry => panic!("expected preserved historical payload, got {{entry:?}}"),
        }}
    }};

    let key = external_key("com.arm.opk.fixture");
    assert_eq!(envelope.get(key, 0), Some(b"fixture-external".as_slice()));
    let roundtrip = Envelope::decode(envelope.serialize()).expect("round trip");
    if let Some((historical_id, historical_bytes)) = preserved_payload {{
        assert!(matches!(roundtrip.entries().next(), Some(EntryRef::Unknown {{ id, bytes }})
            if id == historical_id && bytes == historical_bytes));
    }} else {{
        assert!(roundtrip.get(box_detections, 0).is_some());
    }}
    assert_eq!(roundtrip.get(key, 0), Some(b"fixture-external".as_slice()));

    let future = async move {{ roundtrip.len() }};
    assert_send(future);
}}
'''
    (rust_root / "tests" / "opk_packet.rs").write_text(test_source, encoding="utf-8")


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


def add_typescript_declaration_headers(generated_root: Path) -> None:
    header = f"{TS_LICENSE_HEADER}{TS_GENERATED_HEADER}"
    for declaration in sorted((generated_root / "ts" / "dist").rglob("*.d.ts")):
        text = declaration.read_text(encoding="utf-8")
        if not text.startswith(header):
            declaration.write_text(f"{header}{text}", encoding="utf-8")


def format_cpp_sources(generated_root: Path, clang_format: str) -> None:
    sources = sorted(
        path for path in (generated_root / "cpp").rglob("*")
        if path.suffix in {".h", ".cpp"}
    )
    if not shutil.which(clang_format):
        raise RuntimeError(f"{clang_format} is required to format generated C++ files")
    run([clang_format, f"--style=file:{REPO_ROOT / '.clang-format'}", "-i", *map(str, sources)])


def autopep8_python() -> str:
    if "OPK_DEVTOOLS_VENV" in os.environ:
        candidate = Path(os.environ["OPK_DEVTOOLS_VENV"]) / "bin" / "python"
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


def format_rust_sources(generated_root: Path) -> None:
    sources = sorted((generated_root / "rust").rglob("*.rs"))
    if not shutil.which(RUSTFMT):
        raise RuntimeError(f"{RUSTFMT} is required to format generated Rust files")
    run([RUSTFMT, "--edition", "2021", *map(str, sources)])


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
    rust_manifest = manifests.get("rust")
    typescript_manifest = manifests.get("ts")
    manifests_by_language = (cpp, python_manifest, rust_manifest, typescript_manifest)
    if not all(isinstance(value, dict) for value in manifests_by_language):
        raise RuntimeError(
            "flowdata generation did not produce C++, Python, Rust, and TypeScript manifests"
        )
    shared_fields = (
        "sdk", "generator", "flatc", "schema_files", "schema_set_sha256", "payloads",
    )
    for field in shared_fields:
        if not all(cpp.get(field) == manifest.get(field) for manifest in manifests_by_language[1:]):
            raise RuntimeError(f"flowdata manifests disagree on {field}")
    expected_sdk = {
        "name": config.public_name,
        "version": config.version,
    }
    if cpp.get("generator") != flowdata_source_identity(config)["generator"]:
        raise RuntimeError("flowdata manifest generator identity does not match local sources")
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
        raise RuntimeError("flowdata C++ outputs do not match the open-perception-kit contract")
    if python_outputs != {
        "cpp_python_bridge": False,
        "integrations": [],
        "sdk": "python",
    }:
        raise RuntimeError("flowdata Python outputs do not match the open-perception-kit contract")
    if typescript_manifest.get("outputs") != {
        "cpp_python_bridge": False,
        "integrations": [],
        "sdk": "ts",
    }:
        raise RuntimeError("flowdata TypeScript outputs do not match the open-perception-kit contract")
    if rust_manifest.get("outputs") != {
        "cpp_python_bridge": False,
        "integrations": [],
        "sdk": "rust",
    }:
        raise RuntimeError("flowdata Rust outputs do not match the open-perception-kit contract")


def normalize_integration_files(config: SdkConfig, generated_root: Path) -> None:
    paths = (
        generated_root / "cpp" / "cmake" / f"{config.public_name}.cmake",
        generated_root / "cpp" / "meson" / config.public_name / MESON_BUILD_FILENAME,
        generated_root / "cpp" / "meson" / config.public_name / "python_bridge" / MESON_BUILD_FILENAME,
    )
    for path in paths:
        path.write_text(path.read_text(encoding="utf-8").rstrip() + "\n", encoding="utf-8")


def write_internal_meson(config: SdkConfig, target: Path) -> None:
    cpp_root = os.path.relpath(config.generated_root / "cpp", config.internal_meson_path.parent)
    content = (
        PY_LICENSE_HEADER
        + "\n"
        "# Generated project adapter. Do not edit.\n"
        "# Regenerate with ./scripts/perception-sdk.sh generate.\n\n"
        f"{config.public_name}_version = '{config.version}'\n"
        f"{config.public_name}_flatbuffers_version_requirement = '=={config.flatbuffers_version}'\n\n"
        f"_{config.public_name}_flatbuffers_dep = dependency(\n"
        "  'flatbuffers',\n"
        "  required : true,\n"
        f"  version : {config.public_name}_flatbuffers_version_requirement,\n"
        ")\n"
        f"_{config.public_name}_inc = include_directories('{cpp_root}')\n\n"
        f"{config.public_name}_dep = declare_dependency(\n"
        f"  include_directories : [_{config.public_name}_inc],\n"
        "  compile_args : ['-std=c++20'],\n"
        f"  dependencies : [_{config.public_name}_flatbuffers_dep],\n"
        ")\n"
        "\n"
        "if get_option('tests') or python_ops_enabled\n"
        f"  _{config.public_name}_python = opk_python\n"
        f"  _{config.public_name}_python_embed_dep = _{config.public_name}_python.dependency(\n"
        "    embed : true,\n"
        "    required : true,\n"
        "  )\n"
        f"  _{config.public_name}_python_bridge_sources = files(\n"
        f"    '{cpp_root}/python_bridge/{config.public_name}_python_bridge.cpp',\n"
        "  )\n\n"
        f"  {config.public_name}_python_bridge_dep = declare_dependency(\n"
        f"    include_directories : [_{config.public_name}_inc],\n"
        "    compile_args : ['-std=c++20'],\n"
        f"    dependencies : [{config.public_name}_dep, _{config.public_name}_python_embed_dep],\n"
        f"    sources : _{config.public_name}_python_bridge_sources,\n"
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
        or relative.parts[:2] == ("rust", "target")
        or relative == Path("rust/Cargo.lock")
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
            *(SDK_LEGAL_INPUT_DIR / name for name in SDK_LEGAL_FILES),
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
        "artifact": {"name": config.public_name.replace('_', '-'), "version": config.version},
        "descriptor": {
            "path": config.descriptor_path.relative_to(REPO_ROOT).as_posix(),
            "sha256": config.descriptor_sha256,
        },
        "files": _file_records(generated_root, {manifest_path}),
        "generation": {
            "flowdata_sdk": flowdata_source_identity(config),
            "tools": _generation_tool_records(),
        },
        "upstream_receipts": flowdata_manifests,
        "postprocessing": {
            "copyright_headers": "SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates",
            "cmake_formatter": command_version([CMAKE_FORMAT, "--version"]),
            "cpp_formatter": command_version([clang_format, "--version"]),
            "python_formatter": command_version([formatter_python, "-m", "autopep8", "--version"]),
            "typescript": {
                "compiler": config.typescript_compiler.version,
                "flatbuffers_runtime": config.typescript_runtime.version,
                "node": f">={config.node_minimum_major}",
            },
            "rust": {
                "flatbuffers_runtime": config.flatbuffers_version,
                "formatter": rustfmt_version(),
                "standard_library": True,
            },
        },
        "project_files": [{
            "path": config.internal_meson_path.relative_to(REPO_ROOT).as_posix(),
            "sha256": sha256(internal_meson),
            "size": internal_meson.stat().st_size,
        }],
    }
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _verify_manifest_identity(
    config: SdkConfig,
    generated_root: Path,
    internal_meson: Path,
    path: Path,
    manifest: dict[str, object],
) -> None:
    expected_fields = {
        "artifact", "descriptor", "files", "generation", "postprocessing",
        "project_files", "upstream_receipts",
    }
    if set(manifest) != expected_fields:
        raise RuntimeError("open-perception-kit manifest fields are stale")
    if manifest.get("artifact") != {"name": config.public_name.replace('_', '-'), "version": config.version}:
        raise RuntimeError("open-perception-kit manifest identity does not match sdk.json")
    expected_descriptor = {
        "path": config.descriptor_path.relative_to(REPO_ROOT).as_posix(),
        "sha256": config.descriptor_sha256,
    }
    if manifest.get("descriptor") != expected_descriptor:
        raise RuntimeError("open-perception-kit manifest descriptor identity does not match sdk.json")
    if manifest.get("files") != _file_records(generated_root, {path}):
        raise RuntimeError("open-perception-kit manifest file list or hashes are stale")
    expected_project = [{
        "path": config.internal_meson_path.relative_to(REPO_ROOT).as_posix(),
        "sha256": sha256(internal_meson),
        "size": internal_meson.stat().st_size,
    }]
    if manifest.get("project_files") != expected_project:
        raise RuntimeError("open-perception-kit project integration is stale")


def _verify_generation_identity(config: SdkConfig, manifest: dict[str, object]) -> dict[str, object]:
    generation = manifest.get("generation")
    if not isinstance(generation, dict):
        raise RuntimeError("open-perception-kit generation identity is missing")
    if generation.get("tools") != _generation_tool_records():
        raise RuntimeError("open-perception-kit generation tools changed; regenerate the SDK")
    flowdata_identity = generation.get("flowdata_sdk")
    if not isinstance(flowdata_identity, dict):
        raise RuntimeError("open-perception-kit flowdata identity is missing")
    if flowdata_identity != flowdata_source_identity(config):
        raise RuntimeError("flowdata-sdk changed; regenerate the open-perception-kit")
    return flowdata_identity


def _verify_upstream_receipts(
    config: SdkConfig,
    manifest: dict[str, object],
    flowdata_identity: dict[str, object],
) -> None:
    flowdata = manifest.get("upstream_receipts")
    if not isinstance(flowdata, dict) or set(flowdata) != {"cpp", "python", "rust", "ts"}:
        raise RuntimeError("open-perception-kit manifest has incomplete flowdata metadata")
    expected_sdk = {
        "name": config.public_name,
        "version": config.version,
    }
    for sdk in ("cpp", "python", "rust", "ts"):
        sdk_manifest = flowdata[sdk]
        if not isinstance(sdk_manifest, dict):
            raise RuntimeError(f"{sdk} manifest metadata is malformed")
        if sdk_manifest.get("sdk") != expected_sdk:
            raise RuntimeError(f"{sdk} manifest SDK identity does not match sdk.json")
        if sdk_manifest.get("generator") != flowdata_identity.get("generator"):
            raise RuntimeError(f"{sdk} generator identity is stale")
        if sdk_manifest.get("schema_files") != _schema_records(config.schema_dir):
            raise RuntimeError(f"{sdk} schema inputs are stale")
        if sdk_manifest.get("schema_set_sha256") != _schema_set_sha256(config.schema_dir):
            raise RuntimeError(f"{sdk} schema-set digest is stale")
    cpp_bridge = flowdata["cpp"].get("python_bridge")
    if not isinstance(cpp_bridge, dict) or (
        cpp_bridge.get("sdk_import_name") != config.public_name
    ):
        raise RuntimeError("C++ Python bridge import name is stale")
    _verify_python_receipt(config, flowdata["python"])
    validate_flowdata_manifests(config, flowdata)


def _verify_python_receipt(config: SdkConfig, python_receipt: object) -> None:
    python_package = (
        python_receipt.get("python_package")
        if isinstance(python_receipt, dict) else None
    )
    if not isinstance(python_package, dict) or (
        python_package.get("distribution_name") != config.public_name
        or python_package.get("import_name") != config.public_name
    ):
        raise RuntimeError("Python receipt distribution name is stale")


def verify_perception_manifest(
    config: SdkConfig,
    generated_root: Path | None = None,
    internal_meson: Path | None = None,
) -> dict[str, object]:
    generated_root = generated_root or config.generated_root
    internal_meson = internal_meson or config.internal_meson_path
    path = generated_root / PERCEPTION_MANIFEST_FILENAME
    manifest = json.loads(path.read_text(encoding="utf-8"))
    _verify_manifest_identity(config, generated_root, internal_meson, path, manifest)
    flowdata_identity = _verify_generation_identity(config, manifest)
    _verify_upstream_receipts(config, manifest, flowdata_identity)
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
    prepare_python_package(
        generated_root / "python",
        config.public_name,
        config.version,
        config.python_package_version,
    )
    python_receipt = flowdata_manifests["python"]
    python_package = (
        python_receipt.get("python_package")
        if isinstance(python_receipt, dict) else None
    )
    if not isinstance(python_package, dict) or (
        python_package.get("distribution_name") != config.public_name
        or python_package.get("import_name") != config.public_name
    ):
        raise RuntimeError("generated Python package metadata is unexpected")
    prepare_rust_tests(config, generated_root)
    add_license_headers(generated_root)
    prepare_typescript_package(config, generated_root)
    copy_sdk_legal_files(generated_root)
    format_cpp_sources(generated_root, clang_format)
    format_python_modules(generated_root, formatter_python)
    format_rust_sources(generated_root)
    build_typescript_package(config, generated_root, node, node_modules)
    add_typescript_declaration_headers(generated_root)
    validate_flowdata_manifests(config, flowdata_manifests)
    normalize_integration_files(config, generated_root)
    format_cmake_integrations(generated_root)
    write_internal_meson(config, internal_meson)
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
            if left.suffix in {".json", ".py", ".pyi", ".h", ".cpp", ".rs", ".txt", ".md", ".toml"} or left.name in {MESON_BUILD_FILENAME, "pyproject.toml"}:
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
    internal = workspace / "development" / "perception" / MESON_BUILD_FILENAME
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
            print("Generated open-perception-kit SDKs are stale. Run ./scripts/perception-sdk.sh generate in the container.")
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
        "--version",
        metavar="MAJOR.MINOR.PATCH",
        help="set the product version before regenerating every consumer",
    )
    parser.add_argument(
        "--package-prerelease",
        action="store_true",
        help="mark generated language packages as development prereleases",
    )
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
        if args.check and (args.version or args.package_prerelease):
            raise RuntimeError(
                "--version and --package-prerelease cannot be combined with --check"
            )
        if args.package_prerelease and not args.version:
            raise RuntimeError("--package-prerelease requires --version")
        if args.version:
            set_product_version(PRODUCT_VERSION_PATH, args.version)
            set_package_prerelease(SDK_CONFIG_PATH, args.package_prerelease)
        config = load_sdk_config()
        node_modules = args.node_modules
        if node_modules is None:
            node_modules = Path(command_version(["npm", "root", "--global"]))
        if args.check:
            generated_current = check_generated(
                config, args.flatc, args.python, args.clang_format, args.formatter_python,
                args.node, node_modules,
            )
            consumers_current = synchronize_project_consumers(config, args.node, True)
            return 0 if generated_current and consumers_current else 1
        with tempfile.TemporaryDirectory(prefix=".perception-generate-", dir=REPO_ROOT) as tmp:
            workspace = Path(tmp)
            generated, internal = generate_candidate(
                config, workspace, args.flatc, args.python,
                args.clang_format, args.formatter_python, args.node, node_modules,
            )
            install_candidate(config, generated, internal, workspace)
        synchronize_project_consumers(config, args.node, False)
        verify_perception_manifest(config)
        print(f"Generated {config.public_name} SDK {config.version} in {config.generated_root}")
        return 0
    except (OSError, RuntimeError, subprocess.CalledProcessError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
