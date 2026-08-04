#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

"""Build a deterministic release from the canonical Perception SDK snapshot."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from dataclasses import asdict
from email.parser import Parser
from pathlib import Path

import generate as perception_generate
from artifacts import acquire_artifact
from release_common import command_output, load_json, sha256, validate_relative_path
import sdk_config as perception_config


REPO_ROOT = perception_config.REPO_ROOT
DEFAULT_OUTPUT_DIR = REPO_ROOT / "artifacts"
MANIFEST_FILENAME = "perception-sdk-release-manifest.json"
SOURCE_DATE_EPOCH = "315532800"
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
SEMANTIC_VERSION_RE = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
GIT_COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")


def run(
    command: list[str],
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
) -> None:
    subprocess.run(command, cwd=cwd, env=env, check=True)


def require_semantic_version(version: str) -> None:
    if SEMANTIC_VERSION_RE.fullmatch(version) is None:
        raise RuntimeError("SDK version must use stable MAJOR.MINOR.PATCH form")


def copy_schema_set(config: perception_config.SdkConfig, destination: Path) -> None:
    target = destination / config.schema_dir.relative_to(REPO_ROOT)
    shutil.copytree(config.schema_dir, target)


def generated_flatbuffers_version(generated_manifest: dict[str, object]) -> str:
    try:
        version = generated_manifest["upstream_receipts"]["cpp"]["flatc"]["semantic_version"]
    except (KeyError, TypeError) as exc:
        raise RuntimeError("generated manifest has no FlatBuffers compiler version") from exc
    if not isinstance(version, str):
        raise RuntimeError("generated FlatBuffers compiler version is malformed")
    return version


def acquire_flatbuffers_wheel(
    *,
    generated_manifest: dict[str, object],
    wheel_dir: Path,
    supplied_wheel: Path | None,
    config: perception_config.SdkConfig,
    artifact_dir: Path | None,
) -> Path:
    required_version = generated_flatbuffers_version(generated_manifest)
    lock = config.flatbuffers_wheel
    if lock.version != required_version:
        raise RuntimeError(
            "FlatBuffers wheel lock does not match generated SDK compiler version: "
            f"locked {lock.version}, generated {required_version}"
        )
    return acquire_artifact(
        lock,
        wheel_dir,
        supplied=supplied_wheel,
        cache_dir=artifact_dir,
    )


def create_build_environment(
    *,
    python: str,
    workspace: Path,
    config: perception_config.SdkConfig,
    artifact_dir: Path | None,
) -> str:
    environment = workspace / "python-build-env"
    wheelhouse = workspace / "python-build-tools"
    run([python, "-m", "venv", str(environment)])
    environment_python = environment / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    tool_wheels = [
        acquire_artifact(
            tool,
            wheelhouse,
            cache_dir=artifact_dir,
        )
        for tool in config.python_build_tools
    ]
    run([
        str(environment_python), "-m", "pip", "install", "--no-index", "--no-deps",
        *map(str, tool_wheels),
    ])
    probe = (
        "import importlib.metadata as m, json; "
        "print(json.dumps({name: m.version(name) for name in "
        "('pip', 'setuptools', 'wheel')}, sort_keys=True))"
    )
    installed = json.loads(command_output([str(environment_python), "-c", probe]))
    expected = {tool.name: tool.version for tool in config.python_build_tools}
    if installed != expected:
        raise RuntimeError(f"Python build environment mismatch: {installed} != {expected}")
    return str(environment_python)


def build_perception_wheel(
    *, python: str, python_project: Path, wheel_dir: Path, name: str, version: str,
) -> Path:
    wheel_dir.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update({"PYTHONHASHSEED": "0", "SOURCE_DATE_EPOCH": SOURCE_DATE_EPOCH})
    run([
        python, "-m", "pip", "wheel", "--no-deps", "--no-build-isolation",
        "--wheel-dir", str(wheel_dir), str(python_project),
    ], env=env)
    wheels = sorted(wheel_dir.glob(f"{name}-{version}-*.whl"))
    if len(wheels) != 1:
        raise RuntimeError(f"expected one {name} wheel, found: {wheels}")
    return wheels[0]


def write_requirements(python_dir: Path, config: perception_config.SdkConfig) -> None:
    (python_dir / "requirements.txt").write_text(
        f"{config.name}=={config.version}\n"
        f"flatbuffers=={config.flatbuffers_wheel.version}\n",
        encoding="utf-8",
    )


def write_readme(bundle_root: Path, config: perception_config.SdkConfig) -> None:
    (bundle_root / "README.md").write_text(
        f"""# Perception SDK {config.version}

This archive contains the generated C++ SDK, Python SDK wheel, matching
FlatBuffers Python wheel, source schemas, and release metadata.

## Python

```bash
python3 -m pip install --no-index --find-links python -r python/requirements.txt
```

Use `perception.packet` for serialized packets. `perception.guest` is available
only inside a C++ host that registers the generated live-envelope bridge.

## C++

Use `cpp/cmake/perception.cmake` directly. For Meson, vendor the complete `cpp/`
directory and call `subdir('path/to/cpp/meson/perception')`.
""",
        encoding="utf-8",
    )


def git_commit(repository: Path = REPO_ROOT) -> str:
    return command_output(["git", "-C", str(repository), "rev-parse", "HEAD"])


def repository_git_status() -> str:
    return command_output([
        "git", "-C", str(REPO_ROOT), "status", "--porcelain", "--untracked-files=no",
    ])


def content_digest(value: object) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def bundle_files(bundle_root: Path) -> list[dict[str, object]]:
    manifest_path = bundle_root / MANIFEST_FILENAME
    return [
        {"path": path.relative_to(bundle_root).as_posix(), "sha256": sha256(path), "size": path.stat().st_size}
        for path in sorted(bundle_root.rglob("*"))
        if path.is_file() and path != manifest_path
    ]


def write_bundle_manifest(
    *,
    bundle_root: Path,
    config: perception_config.SdkConfig,
    generated_manifest: dict[str, object],
    generated_manifest_path: Path,
    perception_wheel: Path,
    flatbuffers_wheel: Path,
) -> None:
    cpp_manifest = generated_manifest["upstream_receipts"]["cpp"]
    python_manifest = generated_manifest["upstream_receipts"]["python"]
    manifest = {
        "archive": {
            "compression": "stored", "file_mode": "0644",
            "timestamp": "1980-01-01T00:00:00Z", "top_level_directory": bundle_root.name,
        },
        "artifact": {"name": f"{config.name}-sdk", "version": config.version},
        "files": bundle_files(bundle_root),
        "flatbuffers": {
            "compiler": cpp_manifest["flatc"],
            "python_wheel": {
                **asdict(config.flatbuffers_wheel),
                "path": f"python/{flatbuffers_wheel.name}",
            },
            "runtimes": cpp_manifest["flatbuffers_runtimes"],
        },
        "generator": generated_manifest["generation"]["flowdata_sdk"],
        "outputs": {
            "cpp": cpp_manifest["outputs"], "python": python_manifest["outputs"],
            "python_bridge": cpp_manifest["python_bridge"],
            "python_package": python_manifest["python_package"], "schemas": True,
        },
        "payloads": cpp_manifest["payloads"],
        "perception_wheel": {
            "filename": perception_wheel.name,
            "path": f"python/{perception_wheel.name}", "sha256": sha256(perception_wheel),
        },
        "postprocessing": generated_manifest["postprocessing"],
        "schemas": {
            "files": cpp_manifest["schema_files"],
            "root": config.schema_dir.relative_to(REPO_ROOT).as_posix(),
            "sha256": cpp_manifest["schema_set_sha256"],
        },
        "schema_set_sha256": cpp_manifest["schema_set_sha256"],
        "source": {
            "descriptor": {
                "path": "metadata/sdk.json", "sha256": config.descriptor_sha256,
            },
            "generated_manifest": {
                "path": f"metadata/{generated_manifest_path.name}",
                "sha256": sha256(generated_manifest_path),
            },
            "input_tree_sha256": content_digest({
                "descriptor": config.descriptor_sha256,
                "generated_manifest": sha256(generated_manifest_path),
                "schema_set": cpp_manifest["schema_set_sha256"],
            }),
            "release_tools": [
                {
                    "path": path.relative_to(REPO_ROOT).as_posix(),
                    "sha256": sha256(path),
                }
                for path in (
                    Path(__file__).resolve(),
                    Path(__file__).with_name("artifacts.py").resolve(),
                    Path(__file__).with_name("generate.py").resolve(),
                    Path(__file__).with_name("release_common.py").resolve(),
                    Path(__file__).with_name("sdk_config.py").resolve(),
                )
            ],
        },
        "tools": {
            tool.name: {"version": tool.version, "sha256": tool.sha256}
            for tool in config.python_build_tools
        },
    }
    (bundle_root / MANIFEST_FILENAME).write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8",
    )


def verify_bundle(bundle_root: Path) -> None:
    manifest_path = bundle_root / MANIFEST_FILENAME
    manifest = load_json(manifest_path)
    expected_fields = {
        "archive", "artifact", "files", "flatbuffers", "generator", "outputs",
        "payloads", "perception_wheel", "postprocessing", "schemas",
        "schema_set_sha256", "source", "tools",
    }
    if set(manifest) != expected_fields:
        raise RuntimeError("Perception release manifest fields are stale")
    artifact = manifest.get("artifact")
    if not isinstance(artifact, dict) or not isinstance(artifact.get("version"), str):
        raise RuntimeError("release manifest artifact identity is malformed")
    require_semantic_version(artifact["version"])
    files = manifest.get("files")
    if not isinstance(files, list):
        raise RuntimeError("release manifest files must be a list")
    expected_paths: set[str] = set()
    file_entries: dict[str, dict[str, object]] = {}
    for record in files:
        if not isinstance(record, dict):
            raise RuntimeError("release manifest file record is malformed")
        relative = validate_relative_path(record.get("path"))
        path = bundle_root / relative
        if not path.is_file() or record.get("size") != path.stat().st_size or record.get("sha256") != sha256(path):
            raise RuntimeError(f"release bundle file does not match manifest: {relative}")
        expected_paths.add(relative.as_posix())
        file_entries[relative.as_posix()] = record
    actual_paths = {
        path.relative_to(bundle_root).as_posix()
        for path in bundle_root.rglob("*") if path.is_file() and path != manifest_path
    }
    if actual_paths != expected_paths:
        raise RuntimeError("release bundle contains unmanifested or missing files")
    source = manifest.get("source")
    if not isinstance(source, dict):
        raise RuntimeError("release manifest source metadata is malformed")
    if not SHA256_RE.fullmatch(str(source.get("input_tree_sha256", ""))):
        raise RuntimeError("release manifest input tree identity is malformed")
    for key in ("descriptor", "generated_manifest"):
        identity = source.get(key)
        if not isinstance(identity, dict) or not SHA256_RE.fullmatch(str(identity.get("sha256", ""))):
            raise RuntimeError(f"release manifest {key} identity is malformed")
        path = bundle_root / validate_relative_path(identity.get("path"))
        if not path.is_file() or sha256(path) != identity["sha256"]:
            raise RuntimeError(f"release manifest {key} hash does not match bundled metadata")
    for section in ("perception_wheel",):
        record = manifest.get(section)
        if not isinstance(record, dict):
            raise RuntimeError(f"release manifest {section} is malformed")
        path = bundle_root / validate_relative_path(record.get("path"))
        if not path.is_file() or sha256(path) != record.get("sha256"):
            raise RuntimeError(f"release manifest {section} checksum mismatch")
        if validate_relative_path(record.get("path")).as_posix() not in file_entries:
            raise RuntimeError(f"release manifest {section} is not listed in files")

    verify_manifest_semantics(bundle_root, manifest, file_entries)


def wheel_metadata(path: Path) -> tuple[dict[str, str], list[str], list[str]]:
    with zipfile.ZipFile(path) as wheel:
        metadata_names = [name for name in wheel.namelist() if name.endswith(".dist-info/METADATA")]
        wheel_names = [name for name in wheel.namelist() if name.endswith(".dist-info/WHEEL")]
        if len(metadata_names) != 1 or len(wheel_names) != 1:
            raise RuntimeError(f"wheel metadata layout is malformed: {path.name}")
        metadata = Parser().parsestr(wheel.read(metadata_names[0]).decode("utf-8"))
        wheel_record = Parser().parsestr(wheel.read(wheel_names[0]).decode("utf-8"))
    identity = {
        "name": metadata.get("Name", ""),
        "version": metadata.get("Version", ""),
    }
    return identity, metadata.get_all("Requires-Dist", []), wheel_record.get_all("Tag", [])


def verify_manifest_semantics(
    bundle_root: Path,
    manifest: dict[str, object],
    file_entries: dict[str, dict[str, object]],
) -> None:
    artifact = manifest["artifact"]
    if artifact.get("name") != "perception-sdk":
        raise RuntimeError("release manifest artifact name is invalid")
    source = manifest["source"]
    release_tools = source.get("release_tools")
    if not isinstance(release_tools, list) or not release_tools:
        raise RuntimeError("release manifest release tool identity is missing")
    for tool in release_tools:
        if (
            not isinstance(tool, dict)
            or not SHA256_RE.fullmatch(str(tool.get("sha256", "")))
            or not validate_relative_path(tool.get("path"))
        ):
            raise RuntimeError("release manifest release tool identity is malformed")

    schemas = manifest.get("schemas")
    if not isinstance(schemas, dict):
        raise RuntimeError("release schema metadata is missing")
    schema_root = bundle_root / validate_relative_path(schemas.get("root"))
    schema_files = schemas.get("files")
    if not isinstance(schema_files, list) or not schema_files:
        raise RuntimeError("release schema file records are missing")
    expected_records = []
    for record in schema_files:
        if not isinstance(record, dict):
            raise RuntimeError("release schema file record is malformed")
        relative = validate_relative_path(record.get("path"))
        path = schema_root / relative
        if (
            not path.is_file()
            or record.get("size") != path.stat().st_size
            or record.get("sha256") != sha256(path)
        ):
            raise RuntimeError(f"release schema file does not match: {relative}")
        expected_records.append(record)
    if expected_records != perception_generate._schema_records(schema_root):
        raise RuntimeError("release schema file set is incomplete")
    schema_digest = perception_generate._schema_set_sha256(schema_root)
    if schemas.get("sha256") != schema_digest or manifest.get("schema_set_sha256") != schema_digest:
        raise RuntimeError("release schema-set digest is inconsistent")
    generated_identity = manifest["source"]["generated_manifest"]
    generated = load_json(bundle_root / validate_relative_path(generated_identity["path"]))
    upstream = generated.get("upstream_receipts", {})
    cpp_receipt = upstream.get("cpp", {}) if isinstance(upstream, dict) else {}
    if (
        cpp_receipt.get("schema_files") != schema_files
        or cpp_receipt.get("schema_set_sha256") != schema_digest
    ):
        raise RuntimeError("release schemas do not match the generated SDK receipt")

    perception = manifest.get("perception_wheel")
    flatbuffers = manifest.get("flatbuffers")
    if not isinstance(perception, dict) or not isinstance(flatbuffers, dict):
        raise RuntimeError("release wheel metadata is missing")
    flatbuffers_wheel = flatbuffers.get("python_wheel")
    if not isinstance(flatbuffers_wheel, dict):
        raise RuntimeError("release FlatBuffers wheel metadata is malformed")
    for label, record, expected_name, expected_version in (
        ("Perception", perception, "perception", artifact["version"]),
        ("FlatBuffers", flatbuffers_wheel, "flatbuffers", flatbuffers_wheel.get("version")),
    ):
        path_value = record.get("path")
        relative = validate_relative_path(path_value)
        entry = file_entries.get(relative.as_posix())
        if entry is None or entry.get("sha256") != record.get("sha256"):
            raise RuntimeError(f"release {label} wheel is inconsistent with files")
        identity, requirements, tags = wheel_metadata(bundle_root / relative)
        if identity != {"name": expected_name, "version": expected_version}:
            raise RuntimeError(f"release {label} wheel identity is invalid: {identity}")
        if "py3-none-any" not in tags and "py2.py3-none-any" not in tags:
            raise RuntimeError(f"release {label} wheel tag is not platform independent")
        if label == "Perception" and not any(
            requirement.lower().startswith("flatbuffers") for requirement in requirements
        ):
            raise RuntimeError("Perception wheel does not declare FlatBuffers")


def write_deterministic_zip(bundle_root: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    if temporary.exists():
        temporary.unlink()
    with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_STORED) as archive:
        for path in sorted(candidate for candidate in bundle_root.rglob("*") if candidate.is_file()):
            relative = path.relative_to(bundle_root.parent).as_posix()
            info = zipfile.ZipInfo(relative, ZIP_TIMESTAMP)
            info.compress_type = zipfile.ZIP_STORED
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            info.extra = b""
            info.comment = b""
            archive.writestr(info, path.read_bytes())
    os.replace(temporary, destination)


def verify_zip(bundle_root: Path, archive_path: Path) -> None:
    expected = {
        path.relative_to(bundle_root.parent).as_posix()
        for path in bundle_root.rglob("*") if path.is_file()
    }
    with zipfile.ZipFile(archive_path) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)) or set(names) != expected:
            raise RuntimeError("Perception release ZIP entries do not match staged bundle")
        for info in archive.infolist():
            if (
                info.date_time != ZIP_TIMESTAMP
                or info.compress_type != zipfile.ZIP_STORED
                or info.external_attr != 0o100644 << 16
                or info.extra
                or info.comment
            ):
                raise RuntimeError(f"non-deterministic ZIP metadata: {info.filename}")
            staged = bundle_root.parent / validate_relative_path(info.filename)
            if archive.read(info) != staged.read_bytes():
                raise RuntimeError(f"Perception release ZIP content mismatch: {info.filename}")


def write_provenance(
    archive_path: Path,
    config: perception_config.SdkConfig,
    generated_manifest_path: Path,
    dirty: bool,
) -> Path:
    provenance_path = archive_path.with_suffix(archive_path.suffix + ".provenance.json")
    provenance = {
        "archive": {"path": archive_path.name, "sha256": sha256(archive_path)},
        "descriptor_sha256": config.descriptor_sha256,
        "dirty": dirty,
        "generated_manifest_sha256": sha256(generated_manifest_path),
        "repository_commit": git_commit(),
    }
    provenance_path.write_text(
        json.dumps(provenance, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return provenance_path


def write_checksum(archive_path: Path) -> Path:
    checksum_path = archive_path.with_suffix(archive_path.suffix + ".sha256")
    checksum_path.write_text(
        f"{sha256(archive_path)}  {archive_path.name}\n",
        encoding="utf-8",
    )
    return checksum_path


def verify_release_archive(archive_path: Path) -> None:
    archive_path = archive_path.resolve()
    if not archive_path.is_file():
        raise RuntimeError(f"release archive not found: {archive_path}")
    with tempfile.TemporaryDirectory(prefix="perception-sdk-verify-") as tmp:
        root = Path(tmp)
        with zipfile.ZipFile(archive_path) as archive:
            names = archive.namelist()
            if not names or len(names) != len(set(names)):
                raise RuntimeError("release ZIP entries are empty or duplicated")
            for info in archive.infolist():
                validate_relative_path(info.filename)
                if (
                    info.date_time != ZIP_TIMESTAMP
                    or info.compress_type != zipfile.ZIP_STORED
                    or info.external_attr != 0o100644 << 16
                    or info.extra
                    or info.comment
                ):
                    raise RuntimeError(f"non-deterministic ZIP metadata: {info.filename}")
            archive.extractall(root)
        top_levels = {Path(name).parts[0] for name in names}
        if len(top_levels) != 1:
            raise RuntimeError("release ZIP must contain one top-level directory")
        bundle_root = root / next(iter(top_levels))
        verify_bundle(bundle_root)
        verify_zip(bundle_root, archive_path)


def verify_release_sidecars(archive_path: Path) -> None:
    checksum_path = archive_path.with_suffix(archive_path.suffix + ".sha256")
    expected_checksum = f"{sha256(archive_path)}  {archive_path.name}\n"
    if checksum_path.read_text(encoding="utf-8") != expected_checksum:
        raise RuntimeError("release checksum sidecar does not match archive")
    provenance_path = archive_path.with_suffix(archive_path.suffix + ".provenance.json")
    provenance = load_json(provenance_path)
    if provenance.get("archive") != {
        "path": archive_path.name,
        "sha256": sha256(archive_path),
    }:
        raise RuntimeError("release provenance archive identity is invalid")
    if not isinstance(provenance.get("dirty"), bool):
        raise RuntimeError("release provenance dirty state is invalid")
    if not GIT_COMMIT_RE.fullmatch(str(provenance.get("repository_commit", ""))):
        raise RuntimeError("release provenance repository commit is invalid")
    with zipfile.ZipFile(archive_path) as archive:
        manifest_names = [
            name for name in archive.namelist()
            if name.endswith(f"/{MANIFEST_FILENAME}")
        ]
        if len(manifest_names) != 1:
            raise RuntimeError("release archive manifest layout is invalid")
        manifest = json.loads(archive.read(manifest_names[0]).decode("utf-8"))
    source = manifest.get("source")
    if not isinstance(source, dict):
        raise RuntimeError("release archive source metadata is invalid")
    expected_identities = {
        "descriptor_sha256": source.get("descriptor", {}).get("sha256"),
        "generated_manifest_sha256": source.get("generated_manifest", {}).get("sha256"),
    }
    for field, expected in expected_identities.items():
        if not SHA256_RE.fullmatch(str(expected or "")) or provenance.get(field) != expected:
            raise RuntimeError(f"release provenance {field} does not match the archive")


def build_bundle(args: argparse.Namespace) -> Path:
    config = perception_config.load_sdk_config()
    if args.expect_version is not None and args.expect_version != config.version:
        raise RuntimeError(
            f"expected SDK version {args.expect_version}, descriptor contains {config.version}"
        )
    generated_manifest = perception_generate.verify_perception_manifest(config)
    status = repository_git_status()
    dirty = bool(status)
    if dirty and not args.allow_dirty:
        raise RuntimeError(f"SDK inputs or outputs are dirty:\n{status}")

    output_dir = args.output_dir.resolve()
    archive_path = output_dir / f"{config.name}-sdk-{config.version}.zip"
    with tempfile.TemporaryDirectory(prefix="perception-sdk-release-") as tmp:
        workspace = Path(tmp)
        bundle_root = workspace / f"{config.name}-sdk-{config.version}"
        shutil.copytree(config.generated_root / "cpp", bundle_root / "cpp")
        copy_schema_set(config, bundle_root)
        metadata_dir = bundle_root / "metadata"
        metadata_dir.mkdir(parents=True)
        generated_manifest_path = config.generated_root / perception_generate.PERCEPTION_MANIFEST_FILENAME
        shutil.copy2(generated_manifest_path, metadata_dir / generated_manifest_path.name)
        shutil.copy2(config.descriptor_path, metadata_dir / "sdk.json")

        python_dir = bundle_root / "python"
        build_python = create_build_environment(
            python=args.python,
            workspace=workspace,
            config=config,
            artifact_dir=args.artifact_dir,
        )
        python_project = workspace / "python-source"
        shutil.copytree(
            config.python_project,
            python_project,
            ignore=shutil.ignore_patterns("build", "*.egg-info", "__pycache__", "*.pyc"),
        )
        perception_wheel = build_perception_wheel(
            python=build_python, python_project=python_project, wheel_dir=python_dir,
            name=config.name, version=config.version,
        )
        flatbuffers_wheel = acquire_flatbuffers_wheel(
            generated_manifest=generated_manifest, wheel_dir=python_dir,
            supplied_wheel=args.flatbuffers_wheel, config=config,
            artifact_dir=args.artifact_dir,
        )
        write_requirements(python_dir, config)
        write_readme(bundle_root, config)
        write_bundle_manifest(
            bundle_root=bundle_root, config=config, generated_manifest=generated_manifest,
            generated_manifest_path=generated_manifest_path,
            perception_wheel=perception_wheel, flatbuffers_wheel=flatbuffers_wheel,
        )
        verify_bundle(bundle_root)
        write_deterministic_zip(bundle_root, archive_path)
        verify_zip(bundle_root, archive_path)
    write_provenance(archive_path, config, generated_manifest_path, dirty)
    write_checksum(archive_path)
    return archive_path


def verify_release_path(path: Path, require_sidecars: bool = False) -> None:
    path = path.resolve()
    if path.is_dir():
        verify_bundle(path)
        return
    verify_release_archive(path)
    checksum = path.with_suffix(path.suffix + ".sha256")
    provenance = path.with_suffix(path.suffix + ".provenance.json")
    sidecars = (checksum.exists(), provenance.exists())
    if require_sidecars and not all(sidecars):
        raise RuntimeError("release checksum and provenance sidecars are required")
    if any(sidecars) and not all(sidecars):
        raise RuntimeError("release sidecars are incomplete")
    if all(sidecars):
        verify_release_sidecars(path)


class SdkHelpFormatter(
    argparse.ArgumentDefaultsHelpFormatter,
    argparse.RawDescriptionHelpFormatter,
):
    def _get_help_string(self, action: argparse.Action) -> str:
        if action.default in {None, False, argparse.SUPPRESS} or action.required:
            return action.help
        return super()._get_help_string(action)


def parse_args(argv: list[str]) -> argparse.Namespace:
    if not argv or argv[0] not in {"package", "verify"}:
        argv = ["package", *argv]
    parser = argparse.ArgumentParser(
        prog="./scripts/perception-sdk.sh",
        description=__doc__,
        formatter_class=SdkHelpFormatter,
    )
    commands = parser.add_subparsers(dest="command", required=True)
    package_parser = commands.add_parser(
        "package",
        help="build the SDK release bundle",
        description=(
            "Build a deterministic SDK ZIP from the checked-in generated snapshot. "
            "This command verifies generation metadata but never regenerates SDK files."
        ),
        formatter_class=SdkHelpFormatter,
        epilog=(
            "examples:\n"
            "  ./scripts/perception-sdk.sh package --expect-version 0.1.0\n"
            "  ./scripts/perception-sdk.sh package --output-dir /tmp/sdk "
            "--artifact-dir /tmp/sdk-cache\n\n"
            "The command writes the ZIP, .sha256 checksum, and .provenance.json sidecar."
        ),
    )
    package_parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        metavar="PATH",
        help="directory receiving the release ZIP and checksum/provenance sidecars",
    )
    package_parser.add_argument(
        "--expect-version",
        metavar="MAJOR.MINOR.PATCH",
        help=(
            "fail unless tools/perception/sdk.json contains exactly this SDK version; "
            "does not override the descriptor"
        ),
    )
    package_parser.add_argument(
        "--allow-dirty",
        action="store_true",
        help=(
            "allow tracked repository modifications and record dirty=true in provenance; "
            "intended only for local experiments"
        ),
    )
    package_parser.add_argument(
        "--flatbuffers-wheel",
        type=Path,
        metavar="FILE",
        help=(
            "use this local FlatBuffers wheel instead of acquiring it; filename and "
            "SHA-256 must match the descriptor lock"
        ),
    )
    package_parser.add_argument(
        "--artifact-dir",
        type=Path,
        metavar="PATH",
        help=(
            "read-write cache for checksum-locked Python build tools and FlatBuffers; "
            "missing or invalid artifacts are downloaded"
        ),
    )
    package_parser.add_argument(
        "--python",
        default=sys.executable,
        metavar="EXECUTABLE",
        help="Python interpreter used to create the isolated wheel-build environment",
    )
    verify_parser = commands.add_parser(
        "verify",
        help="verify a bundle directory or ZIP",
        description=(
            "Verify a staged SDK bundle directory or release ZIP. Existing sidecars are "
            "always checked; use --require-sidecars to reject archives without them."
        ),
        formatter_class=SdkHelpFormatter,
        epilog=(
            "example:\n"
            "  ./scripts/perception-sdk.sh verify "
            "artifacts/perception-sdk-0.1.0.zip --require-sidecars"
        ),
    )
    verify_parser.add_argument(
        "path",
        type=Path,
        metavar="PATH",
        help="bundle directory or release ZIP to verify",
    )
    verify_parser.add_argument(
        "--require-sidecars",
        action="store_true",
        help="require and verify both .sha256 and .provenance.json sidecars",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    try:
        args = parse_args(argv)
        if args.command == "verify":
            verify_release_path(args.path, args.require_sidecars)
            print(f"Verified {args.path}")
            return 0
        archive_path = build_bundle(args)
    except (OSError, RuntimeError, subprocess.CalledProcessError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    print(f"Created {archive_path}")
    print(f"SHA256 {sha256(archive_path)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
