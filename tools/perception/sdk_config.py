#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

"""Load and validate the authoritative Perception SDK descriptor."""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from release_common import sha256


REPO_ROOT = Path(__file__).resolve().parents[2]
SDK_CONFIG_PATH = Path(__file__).with_name("sdk.json")
PRODUCT_VERSION_PATH = REPO_ROOT / "development/meson.build"
PYTHON_DISTRIBUTION_NAME = "opk-perception-sdk"
PACKAGE_NAME_RE = re.compile(r"[a-z][a-z0-9_-]*")
SEMVER = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
PRODUCT_VERSION = re.compile(r"project\([^)]*version:\s*'([^']+)'", re.DOTALL)


@dataclass(frozen=True)
class LockedArtifact:
    name: str
    version: str
    filename: str
    url: str
    sha256: str


@dataclass(frozen=True)
class SdkConfig:
    name: str
    version: str
    package_prerelease: bool
    schema_dir: Path
    generated_root: Path
    flatbuffers_version: str
    flatbuffers_wheel: LockedArtifact
    flatbuffers_rust_crates: tuple[LockedArtifact, ...]
    flatbuffers_source: LockedArtifact
    python_build_tools: tuple[LockedArtifact, ...]
    typescript_runtime: LockedArtifact
    typescript_compiler: LockedArtifact
    node_minimum_major: int
    flowdata_root: Path
    flowdata_generator: Path
    internal_meson_path: Path
    descriptor_path: Path
    descriptor_sha256: str

    @property
    def python_project(self) -> Path:
        return self.generated_root / "python"

    @property
    def python_package_version(self) -> str:
        return f"{self.version}.dev0" if self.package_prerelease else self.version

    @property
    def cargo_package_version(self) -> str:
        return f"{self.version}-dev.0" if self.package_prerelease else self.version


def _relative_path(value: object, field: str, base: Path = REPO_ROOT) -> Path:
    if not isinstance(value, str) or not value:
        raise RuntimeError(f"{field} must be a non-empty relative path")
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts or "." in path.parts:
        raise RuntimeError(f"{field} must be a normalized relative path")
    return base.joinpath(*path.parts)


def _artifact(value: object, field: str, name: str, version: str) -> LockedArtifact:
    expected = {"filename", "sha256", "url"}
    if not isinstance(value, dict) or set(value) != expected:
        raise RuntimeError(f"{field} fields must be exactly: {sorted(expected)}")
    filename = value["filename"]
    url = value["url"]
    digest = value["sha256"]
    if not isinstance(filename, str) or not filename or "/" in filename or "\\" in filename:
        raise RuntimeError(f"{field}.filename must be a plain filename")
    if not isinstance(url, str) or not url.startswith("https://") or not url.endswith(filename):
        raise RuntimeError(f"{field}.url must be HTTPS and end with its filename")
    if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise RuntimeError(f"{field}.sha256 must be lowercase hexadecimal")
    return LockedArtifact(name, version, filename, url, digest)


def _named_artifact(value: object, field: str, expected_name: str) -> LockedArtifact:
    expected = {"name", "version", "filename", "url", "sha256"}
    if not isinstance(value, dict) or set(value) != expected:
        raise RuntimeError(f"{field} has invalid fields")
    name = value["name"]
    version = value["version"]
    if name != expected_name:
        raise RuntimeError(f"{field}.name must be {expected_name}")
    if not isinstance(version, str) or not SEMVER.fullmatch(version):
        raise RuntimeError(f"{field}.version must be semantic")
    return _artifact(
        {key: value[key] for key in ("filename", "url", "sha256")},
        field,
        name,
        version,
    )


def _python_build_tools(value: object) -> tuple[LockedArtifact, ...]:
    if not isinstance(value, dict) or set(value) != {"tools"}:
        raise RuntimeError("python_build must contain only tools")
    tools = value["tools"]
    if not isinstance(tools, list) or not tools:
        raise RuntimeError("python_build.tools must be a non-empty list")
    artifacts: list[LockedArtifact] = []
    for index, tool in enumerate(tools):
        field = f"python_build.tools[{index}]"
        if not isinstance(tool, dict):
            raise RuntimeError(f"{field} has invalid fields")
        name = tool.get("name")
        if not isinstance(name, str) or PACKAGE_NAME_RE.fullmatch(name) is None:
            raise RuntimeError(f"{field}.name is invalid")
        artifacts.append(_named_artifact(tool, field, name))
    if [artifact.name for artifact in artifacts] != ["pip", "setuptools", "wheel"]:
        raise RuntimeError("python_build.tools must contain pip, setuptools, and wheel in order")
    return tuple(artifacts)


def _rust_crates(value: object, flatbuffers_version: str) -> tuple[LockedArtifact, ...]:
    if not isinstance(value, list) or not value:
        raise RuntimeError("flatbuffers.rust_crates must be a non-empty list")
    artifacts: list[LockedArtifact] = []
    for index, crate in enumerate(value):
        field = f"flatbuffers.rust_crates[{index}]"
        if not isinstance(crate, dict):
            raise RuntimeError(f"{field} has invalid fields")
        name = crate.get("name")
        if not isinstance(name, str) or PACKAGE_NAME_RE.fullmatch(name) is None:
            raise RuntimeError(f"{field}.name is invalid")
        artifact = _named_artifact(crate, field, name)
        if artifact.filename != f"{artifact.name}-{artifact.version}.crate":
            raise RuntimeError(f"{field}.filename must match the Cargo crate identity")
        artifacts.append(artifact)
    names = [artifact.name for artifact in artifacts]
    if len(names) != len(set(names)):
        raise RuntimeError("flatbuffers.rust_crates names must be unique")
    if artifacts[0].name != "flatbuffers" or artifacts[0].version != flatbuffers_version:
        raise RuntimeError("the first Rust crate must be the locked FlatBuffers runtime")
    return tuple(artifacts)


def _typescript_build(
    value: object, flatbuffers_version: str
) -> tuple[LockedArtifact, LockedArtifact, int]:
    expected = {"flatbuffers_runtime", "node_minimum_major", "typescript"}
    if not isinstance(value, dict) or set(value) != expected:
        raise RuntimeError(
            "typescript_build fields must be exactly: "
            "['flatbuffers_runtime', 'node_minimum_major', 'typescript']"
        )
    node_minimum_major = value["node_minimum_major"]
    if not isinstance(node_minimum_major, int) or node_minimum_major < 20:
        raise RuntimeError("typescript_build.node_minimum_major must be at least 20")
    runtime = _named_artifact(
        value["flatbuffers_runtime"], "typescript_build.flatbuffers_runtime", "flatbuffers"
    )
    if runtime.version != flatbuffers_version:
        raise RuntimeError("TypeScript FlatBuffers runtime must match the compiler version")
    compiler = _named_artifact(
        value["typescript"], "typescript_build.typescript", "typescript"
    )
    return runtime, compiler, node_minimum_major


def product_version(path: Path = PRODUCT_VERSION_PATH) -> str:
    match = PRODUCT_VERSION.search(path.read_text(encoding="utf-8"))
    if not match or not SEMVER.fullmatch(match.group(1)):
        raise RuntimeError(
            "development/meson.build must contain a stable MAJOR.MINOR.PATCH version"
        )
    return match.group(1)


def load_sdk_config(path: Path = SDK_CONFIG_PATH) -> SdkConfig:
    raw = json.loads(path.read_text(encoding="utf-8"))
    expected = {
        "name",
        "package_prerelease",
        "schema_dir",
        "generated_dir",
        "flatbuffers",
        "flowdata_sdk",
        "project_generated_files",
        "python_build",
        "typescript_build",
    }
    if not isinstance(raw, dict) or set(raw) != expected:
        raise RuntimeError(f"SDK descriptor fields must be exactly: {sorted(expected)}")

    name = raw["name"]
    if not isinstance(name, str) or PACKAGE_NAME_RE.fullmatch(name) is None:
        raise RuntimeError("SDK name must be a lowercase package identifier")
    package_prerelease = raw["package_prerelease"]
    if not isinstance(package_prerelease, bool):
        raise RuntimeError("package_prerelease must be boolean")

    flatbuffers = raw["flatbuffers"]
    if not isinstance(flatbuffers, dict) or set(flatbuffers) != {
        "version", "python_wheel", "rust_crates", "source_archive"
    }:
        raise RuntimeError(
            "flatbuffers fields must be exactly: "
            "['python_wheel', 'rust_crates', 'source_archive', 'version']"
        )
    flatbuffers_version = flatbuffers["version"]
    if not isinstance(flatbuffers_version, str) or not SEMVER.fullmatch(flatbuffers_version):
        raise RuntimeError("FlatBuffers wheel version must be semantic")

    wheel = _artifact(
        flatbuffers["python_wheel"], "flatbuffers.python_wheel",
        "flatbuffers", flatbuffers_version,
    )
    source = _artifact(
        flatbuffers["source_archive"], "flatbuffers.source_archive",
        "flatbuffers", flatbuffers_version,
    )
    rust_crates = _rust_crates(flatbuffers["rust_crates"], flatbuffers_version)

    build_tools = _python_build_tools(raw["python_build"])
    typescript_runtime, typescript_compiler, node_minimum_major = _typescript_build(
        raw["typescript_build"], flatbuffers_version
    )

    flowdata = raw["flowdata_sdk"]
    if not isinstance(flowdata, dict) or set(flowdata) != {"root", "generator"}:
        raise RuntimeError("flowdata_sdk fields must be exactly: ['generator', 'root']")
    flowdata_root = _relative_path(flowdata["root"], "flowdata_sdk.root")
    flowdata_generator = _relative_path(
        flowdata["generator"], "flowdata_sdk.generator", flowdata_root
    )

    project_files = raw["project_generated_files"]
    if not isinstance(project_files, dict) or set(project_files) != {"internal_meson"}:
        raise RuntimeError("project_generated_files must contain only internal_meson")

    schema_dir = _relative_path(raw["schema_dir"], "schema_dir")
    generated_root = _relative_path(raw["generated_dir"], "generated_dir")
    internal_meson_path = _relative_path(
        project_files["internal_meson"], "project_generated_files.internal_meson"
    )
    if not schema_dir.is_dir():
        raise RuntimeError(f"schema directory does not exist: {schema_dir}")

    return SdkConfig(
        name=name,
        version=product_version(),
        package_prerelease=package_prerelease,
        schema_dir=schema_dir,
        generated_root=generated_root,
        flatbuffers_version=flatbuffers_version,
        flatbuffers_wheel=wheel,
        flatbuffers_rust_crates=rust_crates,
        flatbuffers_source=source,
        python_build_tools=build_tools,
        typescript_runtime=typescript_runtime,
        typescript_compiler=typescript_compiler,
        node_minimum_major=node_minimum_major,
        flowdata_root=flowdata_root,
        flowdata_generator=flowdata_generator,
        internal_meson_path=internal_meson_path,
        descriptor_path=path,
        descriptor_sha256=sha256(path),
    )
