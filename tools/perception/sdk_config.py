#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

"""Load and validate the authoritative Perception SDK descriptor."""

from __future__ import annotations

import configparser
import json
import re
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from release_common import sha256


REPO_ROOT = Path(__file__).resolve().parents[2]
SDK_CONFIG_PATH = Path(__file__).with_name("sdk.json")
SEMVER = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")


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
    schema_dir: Path
    generated_root: Path
    flatbuffers_version: str
    flatbuffers_wheel: LockedArtifact
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


def _relative_path(value: object, field: str, base: Path = REPO_ROOT) -> Path:
    if not isinstance(value, str) or not value:
        raise RuntimeError(f"{field} must be a non-empty relative path")
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts or "." in path.parts:
        raise RuntimeError(f"{field} must be a normalized relative path")
    return base.joinpath(*path.parts)


def _submodule_path(name: object) -> Path:
    if not isinstance(name, str) or not name:
        raise RuntimeError("flowdata_sdk.submodule must be a non-empty string")
    modules = configparser.ConfigParser()
    modules.read(REPO_ROOT / ".gitmodules", encoding="utf-8")
    section = f'submodule "{name}"'
    if section not in modules or "path" not in modules[section]:
        raise RuntimeError(f"unknown flowdata-sdk submodule: {name}")
    return _relative_path(modules[section]["path"], f".gitmodules {section}.path")


def load_sdk_config(path: Path = SDK_CONFIG_PATH) -> SdkConfig:
    raw = json.loads(path.read_text(encoding="utf-8"))
    expected = {
        "name",
        "version",
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
    version = raw["version"]
    if not isinstance(name, str) or not re.fullmatch(r"[a-z][a-z0-9_-]*", name):
        raise RuntimeError("SDK name must be a lowercase package identifier")
    if not isinstance(version, str) or not SEMVER.fullmatch(version):
        raise RuntimeError("SDK version must be a stable semantic version (MAJOR.MINOR.PATCH)")

    flatbuffers = raw["flatbuffers"]
    if not isinstance(flatbuffers, dict) or set(flatbuffers) != {
        "version", "python_wheel", "source_archive"
    }:
        raise RuntimeError(
            "flatbuffers fields must be exactly: "
            "['python_wheel', 'source_archive', 'version']"
        )
    flatbuffers_version = flatbuffers["version"]
    if not isinstance(flatbuffers_version, str) or not SEMVER.fullmatch(flatbuffers_version):
        raise RuntimeError("FlatBuffers wheel version must be semantic")

    def artifact(value: object, field: str, name: str, version_value: str) -> LockedArtifact:
        expected_artifact = {"filename", "sha256", "url"}
        if not isinstance(value, dict) or set(value) != expected_artifact:
            raise RuntimeError(f"{field} fields must be exactly: {sorted(expected_artifact)}")
        filename = value["filename"]
        url = value["url"]
        digest = value["sha256"]
        if not isinstance(filename, str) or not filename or "/" in filename or "\\" in filename:
            raise RuntimeError(f"{field}.filename must be a plain filename")
        if not isinstance(url, str) or not url.startswith("https://") or not url.endswith(filename):
            raise RuntimeError(f"{field}.url must be HTTPS and end with its filename")
        if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise RuntimeError(f"{field}.sha256 must be lowercase hexadecimal")
        return LockedArtifact(name, version_value, filename, url, digest)

    wheel = artifact(
        flatbuffers["python_wheel"], "flatbuffers.python_wheel",
        "flatbuffers", flatbuffers_version,
    )
    source = artifact(
        flatbuffers["source_archive"], "flatbuffers.source_archive",
        "flatbuffers", flatbuffers_version,
    )

    python_build = raw["python_build"]
    if not isinstance(python_build, dict) or set(python_build) != {"tools"}:
        raise RuntimeError("python_build must contain only tools")
    tools = python_build["tools"]
    if not isinstance(tools, list) or not tools:
        raise RuntimeError("python_build.tools must be a non-empty list")
    build_tools: list[LockedArtifact] = []
    for index, value in enumerate(tools):
        field = f"python_build.tools[{index}]"
        if not isinstance(value, dict) or set(value) != {
            "name", "version", "filename", "url", "sha256"
        }:
            raise RuntimeError(f"{field} has invalid fields")
        name_value = value["name"]
        version_value = value["version"]
        if not isinstance(name_value, str) or not re.fullmatch(r"[a-z][a-z0-9_-]*", name_value):
            raise RuntimeError(f"{field}.name is invalid")
        if not isinstance(version_value, str) or not SEMVER.fullmatch(version_value):
            raise RuntimeError(f"{field}.version must be semantic")
        build_tools.append(artifact(
            {key: value[key] for key in ("filename", "url", "sha256")},
            field, name_value, version_value,
        ))
    if [tool.name for tool in build_tools] != ["pip", "setuptools", "wheel"]:
        raise RuntimeError("python_build.tools must contain pip, setuptools, and wheel in order")

    typescript_build = raw["typescript_build"]
    if not isinstance(typescript_build, dict) or set(typescript_build) != {
        "flatbuffers_runtime", "node_minimum_major", "typescript"
    }:
        raise RuntimeError(
            "typescript_build fields must be exactly: "
            "['flatbuffers_runtime', 'node_minimum_major', 'typescript']"
        )
    node_minimum_major = typescript_build["node_minimum_major"]
    if not isinstance(node_minimum_major, int) or node_minimum_major < 20:
        raise RuntimeError("typescript_build.node_minimum_major must be at least 20")

    def npm_artifact(value: object, field: str, expected_name: str) -> LockedArtifact:
        if not isinstance(value, dict) or set(value) != {
            "name", "version", "filename", "url", "sha256"
        }:
            raise RuntimeError(f"{field} has invalid fields")
        name_value = value["name"]
        version_value = value["version"]
        if name_value != expected_name:
            raise RuntimeError(f"{field}.name must be {expected_name}")
        if not isinstance(version_value, str) or not SEMVER.fullmatch(version_value):
            raise RuntimeError(f"{field}.version must be semantic")
        return artifact(
            {key: value[key] for key in ("filename", "url", "sha256")},
            field,
            name_value,
            version_value,
        )

    typescript_runtime = npm_artifact(
        typescript_build["flatbuffers_runtime"],
        "typescript_build.flatbuffers_runtime",
        "flatbuffers",
    )
    if typescript_runtime.version != flatbuffers_version:
        raise RuntimeError(
            "TypeScript FlatBuffers runtime must match the compiler version"
        )
    typescript_compiler = npm_artifact(
        typescript_build["typescript"],
        "typescript_build.typescript",
        "typescript",
    )

    flowdata = raw["flowdata_sdk"]
    if not isinstance(flowdata, dict) or set(flowdata) != {"submodule", "generator"}:
        raise RuntimeError("flowdata_sdk fields must be exactly: ['generator', 'submodule']")
    flowdata_root = _submodule_path(flowdata["submodule"])
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
        version=version,
        schema_dir=schema_dir,
        generated_root=generated_root,
        flatbuffers_version=flatbuffers_version,
        flatbuffers_wheel=wheel,
        flatbuffers_source=source,
        python_build_tools=tuple(build_tools),
        typescript_runtime=typescript_runtime,
        typescript_compiler=typescript_compiler,
        node_minimum_major=node_minimum_major,
        flowdata_root=flowdata_root,
        flowdata_generator=flowdata_generator,
        internal_meson_path=internal_meson_path,
        descriptor_path=path,
        descriptor_sha256=sha256(path),
    )
