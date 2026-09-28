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

"""Build a deterministic release from the canonical open-perception-kit snapshot."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import tarfile
import zipfile
from dataclasses import asdict
from email.parser import Parser
from pathlib import Path, PurePosixPath
from typing import cast

import generate as perception_generate
from artifacts import acquire_artifact
from release_common import command_output, load_json, sha256, validate_relative_path
import sdk_config as perception_config


REPO_ROOT = perception_config.REPO_ROOT
DEFAULT_OUTPUT_DIR = REPO_ROOT / "artifacts"
MANIFEST_FILENAME = "open-perception-kit-release-manifest.json"
SOURCE_DATE_EPOCH = "315532800"
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
SEMANTIC_VERSION_RE = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$", re.ASCII
)
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
GIT_COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
CHECKSUM_SUFFIX = ".sha256"
PROVENANCE_SUFFIX = ".provenance.json"
MAX_NPM_ARCHIVE_BYTES = 64 * 1024 * 1024
MAX_NPM_EXPANDED_BYTES = 256 * 1024 * 1024
MAX_NPM_MEMBERS = 10_000
MAX_NPM_METADATA_BYTES = 1024 * 1024
MAX_RUST_CRATE_EXPANDED_BYTES = 64 * 1024 * 1024
MAX_RUST_CRATE_MEMBERS = 10_000
RUST_CARGO_CONFIG = """\
[source.crates-io]
replace-with = "open-perception-kit-vendor"

[source.open-perception-kit-vendor]
directory = "vendor"

[net]
offline = true
"""


def cargo_lock_packages(path: Path) -> dict[tuple[str, str], str | None]:
    packages: dict[tuple[str, str], str | None] = {}
    text = path.read_text(encoding="utf-8")
    for block in re.split(r"(?m)^\[\[package\]\]\s*$", text)[1:]:
        name = re.search(r'(?m)^name = "([^"]+)"$', block)
        version = re.search(r'(?m)^version = "([^"]+)"$', block)
        checksum = re.search(r'(?m)^checksum = "([0-9a-f]{64})"$', block)
        if name is None or version is None:
            raise RuntimeError("release Rust Cargo lock is malformed")
        packages[(name.group(1), version.group(1))] = (
            checksum.group(1) if checksum is not None else None
        )
    if not packages:
        raise RuntimeError("release Rust Cargo lock is malformed")
    return packages


def directory_file_hashes(root: Path) -> dict[str, str]:
    return {
        path.relative_to(root).as_posix(): sha256(path)
        for path in sorted(root.rglob("*"))
        if path.is_file()
    }


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


def copy_rust_sdk(source: Path, destination: Path) -> None:
    shutil.copytree(
        source,
        destination,
        ignore=shutil.ignore_patterns("Cargo.lock", "target"),
    )


def _rust_crate_member_path(
    member: tarfile.TarInfo, expected_root: str
) -> PurePosixPath | None:
    path = PurePosixPath(member.name)
    if path.is_absolute() or ".." in path.parts or not path.parts:
        raise RuntimeError(f"Rust crate contains unsafe path: {member.name}")
    if path.parts[0] != expected_root:
        raise RuntimeError(
            f"Rust crate root does not match package identity: {member.name}"
        )
    relative = PurePosixPath(*path.parts[1:])
    return relative if relative.parts else None


def _extract_rust_crate_file(
    archive: tarfile.TarFile,
    member: tarfile.TarInfo,
    target: Path,
) -> str:
    if not member.isfile():
        raise RuntimeError(f"Rust crate contains unsupported member: {member.name}")
    source = archive.extractfile(member)
    if source is None:
        raise RuntimeError(f"Rust crate member cannot be read: {member.name}")
    target.parent.mkdir(parents=True, exist_ok=True)
    with source, target.open("wb") as output:
        shutil.copyfileobj(source, output)
    return sha256(target)


def extract_rust_crate(
    crate: Path,
    destination: Path,
    artifact: perception_config.LockedArtifact,
) -> None:
    expected_root = f"{artifact.name}-{artifact.version}"
    file_hashes: dict[str, str] = {}
    member_paths: set[str] = set()
    with tarfile.open(crate, "r:gz") as archive:  # NOSONAR
        members = archive.getmembers()
        if len(members) > MAX_RUST_CRATE_MEMBERS:
            raise RuntimeError(f"Rust crate has too many archive members: {crate.name}")
        expanded_size = sum(member.size for member in members if member.isfile())
        if expanded_size > MAX_RUST_CRATE_EXPANDED_BYTES:
            raise RuntimeError(f"Rust crate expands beyond the size limit: {crate.name}")
        for member in members:
            relative = _rust_crate_member_path(member, expected_root)
            if relative is None:
                continue
            relative_path = relative.as_posix()
            if relative_path in member_paths:
                raise RuntimeError(
                    f"Rust crate contains duplicate archive path: {member.name}"
                )
            member_paths.add(relative_path)
            target = destination.joinpath(*relative.parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            file_hashes[relative_path] = _extract_rust_crate_file(
                archive, member, target
            )
    if not file_hashes:
        raise RuntimeError(f"Rust crate contains no files: {crate.name}")
    (destination / ".cargo-checksum.json").write_text(
        json.dumps({"files": file_hashes, "package": artifact.sha256}, sort_keys=True),
        encoding="utf-8",
    )


def prepare_rust_vendor(
    *,
    rust_root: Path,
    workspace: Path,
    config: perception_config.SdkConfig,
    artifact_dir: Path | None,
) -> list[Path]:
    crates_dir = rust_root / "crates"
    vendor_dir = rust_root / "vendor"
    crate_paths: list[Path] = []
    for artifact in config.flatbuffers_rust_crates:
        crate = acquire_artifact(artifact, crates_dir, cache_dir=artifact_dir)
        crate_paths.append(crate)
        extract_rust_crate(crate, vendor_dir / f"{artifact.name}-{artifact.version}", artifact)
    cargo_config = rust_root / ".cargo" / "config.toml"
    cargo_config.parent.mkdir(parents=True, exist_ok=True)
    cargo_config.write_text(RUST_CARGO_CONFIG, encoding="utf-8")
    cargo_home = workspace / "cargo-home"
    cargo_home.mkdir()
    environment = dict(os.environ)
    environment["CARGO_HOME"] = str(cargo_home)
    run(
        [
            "cargo",
            "generate-lockfile",
            "--offline",
            "--manifest-path",
            str(rust_root / "Cargo.toml"),
        ],
        cwd=rust_root,
        env=environment,
    )
    return crate_paths


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
    wheels = sorted(wheel_dir.glob("*.whl"))
    expected_name = f"{name}-{version}-py3-none-any.whl"
    if len(wheels) != 1 or wheels[0].name != expected_name:
        raise RuntimeError(f"expected wheel {expected_name}, found: {wheels}")
    return wheels[0]


def write_requirements(python_dir: Path, config: perception_config.SdkConfig) -> None:
    (python_dir / "requirements.txt").write_text(
        f"{config.public_name}=={config.python_package_version}\n"
        f"flatbuffers=={config.flatbuffers_wheel.version}\n",
        encoding="utf-8",
    )


def write_readme(bundle_root: Path, config: perception_config.SdkConfig) -> None:
    (bundle_root / "README.md").write_text(
        f"""# open-perception-kit {config.version}

This archive contains the generated C++ SDK, Python SDK wheel, Rust crate,
TypeScript SDK package, matching FlatBuffers runtimes, source schemas, and
release metadata.

## Python

```bash
python3 -m pip install --no-index --find-links python -r python/requirements.txt
```

Use `open_perception_kit.packet` for serialized packets. `open_perception_kit.guest` is available
only inside a C++ host that registers the generated live-envelope bridge.

## C++

Use `cpp/cmake/{config.public_name}.cmake` directly. For Meson, vendor the complete `cpp/`
directory and call `subdir('path/to/cpp/meson/{config.public_name}')`.

## Rust

Add the extracted `rust/` directory as a path dependency:

```toml
[dependencies]
{config.public_name} = {{ path = "/path/to/{config.public_name.replace('_', '-')}-{config.version}/rust" }}
```

For an offline consumer build, copy `rust/.cargo/config.toml` into the
consumer's `.cargo/config.toml`, change `directory` to the absolute extracted
`rust/vendor` path, then generate and use the consumer lockfile:

```bash
cargo generate-lockfile --offline
cargo build --offline --locked
```

Import `Envelope`, `payload`, and generated native payload types from the
`{config.public_name}` crate. Require successful `Envelope::decode(...)` and an exact
producer identity match before typed payload access.

## TypeScript

```bash
npm install ./typescript/flatbuffers-{config.typescript_runtime.version}.tgz \\
  ./typescript/{config.public_name.replace('_', '-')}-{config.cargo_package_version}.tgz
```

Import `Envelope` and generated payload classes from `{config.public_name.replace('_', '-')}`. Require an
exact producer identity match before typed payload access.

## Licensing

See [Licensing](LICENSING.md), [LICENSE](LICENSE), and [NOTICE](NOTICE).
The Python wheels, npm archives, and Rust vendor directories retain the original
licences and notices of FlatBuffers and its runtime dependencies.
""",
        encoding="utf-8",
    )


def write_deterministic_npm_package(source: Path, destination: Path) -> None:
    files = [source / name for name in ("package.json", *perception_generate.SDK_LEGAL_FILES)]
    for directory in ("dist", "src"):
        files.extend(path for path in sorted((source / directory).rglob("*")) if path.is_file())
    destination.parent.mkdir(parents=True, exist_ok=True)
    buffer = io.BytesIO()
    with tarfile.TarFile(fileobj=buffer, mode="w", format=tarfile.PAX_FORMAT) as archive:
        for path in files:
            relative = Path("package") / path.relative_to(source)
            info = tarfile.TarInfo(relative.as_posix())
            content = path.read_bytes()
            info.size = len(content)
            info.mode = 0o644
            info.mtime = 0
            info.uid = 0
            info.gid = 0
            info.uname = ""
            info.gname = ""
            archive.addfile(info, io.BytesIO(content))
    with destination.open("wb") as output:
        with gzip.GzipFile(filename="", mode="wb", fileobj=output, mtime=0) as compressed:
            compressed.write(buffer.getvalue())


def _read_npm_metadata_member(
    archive: tarfile.TarFile, member: tarfile.TarInfo, package_name: str
) -> bytes | None:
    if member.name != "package/package.json":
        return None
    if not member.isfile():
        raise RuntimeError(f"npm package metadata is invalid: {package_name}")
    if member.size > MAX_NPM_METADATA_BYTES:
        raise RuntimeError(f"npm package metadata is too large: {package_name}")
    package_file = archive.extractfile(member)
    if package_file is None:
        raise RuntimeError(f"npm package metadata is unreadable: {package_name}")
    return package_file.read(MAX_NPM_METADATA_BYTES + 1)


def npm_package_metadata(path: Path) -> dict[str, object]:
    if path.stat().st_size > MAX_NPM_ARCHIVE_BYTES:
        raise RuntimeError(f"npm package archive is too large: {path.name}")

    with (
        path.open("rb") as source,
        gzip.GzipFile(fileobj=source, mode="rb") as compressed,
        tarfile.TarFile(fileobj=compressed, mode="r") as archive,
    ):
        package_bytes: bytes | None = None
        member_count = 0
        expanded_bytes = 0
        for member in archive:
            if member.name.startswith("/") or ".." in Path(member.name).parts:
                raise RuntimeError(f"npm package contains an unsafe path: {path.name}")
            member_count += 1
            expanded_bytes += member.size
            if member_count > MAX_NPM_MEMBERS or expanded_bytes > MAX_NPM_EXPANDED_BYTES:
                raise RuntimeError(f"npm package expands beyond safety limits: {path.name}")
            candidate = _read_npm_metadata_member(archive, member, path.name)
            if candidate is None:
                continue
            if package_bytes is not None:
                raise RuntimeError(f"npm package metadata is invalid: {path.name}")
            package_bytes = candidate

        if package_bytes is None:
            raise RuntimeError(f"npm package has no package.json: {path.name}")
        package = json.loads(package_bytes.decode("utf-8"))
    return package


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
    perception_npm_package: Path,
    flatbuffers_npm_package: Path,
    rust_crates: list[Path],
) -> None:
    if len(rust_crates) != len(config.flatbuffers_rust_crates):
        raise RuntimeError("packaged Rust crate artifact set is incomplete")
    cpp_manifest = generated_manifest["upstream_receipts"]["cpp"]
    python_manifest = generated_manifest["upstream_receipts"]["python"]
    rust_manifest = generated_manifest["upstream_receipts"]["rust"]
    typescript_manifest = generated_manifest["upstream_receipts"]["ts"]
    runtime_records = {
        (record["language"], record["package"], record["version_requirement"]): record
        for receipt in (cpp_manifest, python_manifest, rust_manifest, typescript_manifest)
        for record in receipt["flatbuffers_runtimes"]
    }
    manifest = {
        "archive": {
            "compression": "stored", "file_mode": "0644",
            "timestamp": "1980-01-01T00:00:00Z", "top_level_directory": bundle_root.name,
        },
        "artifact": {"name": config.public_name.replace('_', '-'), "version": config.version},
        "files": bundle_files(bundle_root),
        "flatbuffers": {
            "compiler": cpp_manifest["flatc"],
            "python_wheel": {
                **asdict(config.flatbuffers_wheel),
                "path": f"python/{flatbuffers_wheel.name}",
            },
            "rust_crates": [
                {
                    **asdict(artifact),
                    "path": f"rust/crates/{crate.name}",
                }
                for artifact, crate in zip(config.flatbuffers_rust_crates, rust_crates)
            ],
            "source_archive": asdict(config.flatbuffers_source),
            "typescript_package": {
                **asdict(config.typescript_runtime),
                "path": f"typescript/{flatbuffers_npm_package.name}",
            },
            "runtimes": [runtime_records[key] for key in sorted(runtime_records)],
        },
        "generator": generated_manifest["generation"]["flowdata_sdk"],
        "outputs": {
            "cpp": cpp_manifest["outputs"], "python": python_manifest["outputs"],
            "rust": rust_manifest["outputs"],
            "typescript": typescript_manifest["outputs"],
            "python_bridge": cpp_manifest["python_bridge"],
            "python_package": {
                **python_manifest["python_package"],
                "distribution_name": config.public_name,
                "import_name": config.public_name,
                "version": config.python_package_version,
            },
            "schemas": True,
        },
        "payloads": cpp_manifest["payloads"],
        "perception_wheel": {
            "filename": perception_wheel.name,
            "path": f"python/{perception_wheel.name}", "sha256": sha256(perception_wheel),
        },
        "perception_npm_package": {
            "filename": perception_npm_package.name,
            "path": f"typescript/{perception_npm_package.name}",
            "sha256": sha256(perception_npm_package),
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


def _verify_manifest_files(
    bundle_root: Path, manifest_path: Path, files: object
) -> dict[str, dict[str, object]]:
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
    return file_entries


def _bundled_source_metadata(bundle_root: Path, source: dict[str, object]) -> dict[str, dict[str, object]]:
    metadata: dict[str, dict[str, object]] = {}
    for key in ("descriptor", "generated_manifest"):
        identity = source.get(key)
        if not isinstance(identity, dict) or not SHA256_RE.fullmatch(str(identity.get("sha256", ""))):
            raise RuntimeError(f"release manifest {key} identity is malformed")
        path = bundle_root / validate_relative_path(identity.get("path"))
        if not path.is_file() or sha256(path) != identity["sha256"]:
            raise RuntimeError(f"release manifest {key} hash does not match bundled metadata")
        metadata[key] = load_json(path)
    return metadata


def _verify_source_identities(
    bundle_root: Path,
    source: object,
    artifact: dict[str, object],
    schema_set_sha256: object,
    manifest_flatbuffers: object,
    manifest_generator: object,
) -> dict[str, object]:
    if not isinstance(source, dict):
        raise RuntimeError("release manifest source metadata is malformed")
    metadata = _bundled_source_metadata(bundle_root, source)

    expected_input_tree = content_digest({
        "descriptor": source["descriptor"]["sha256"],
        "generated_manifest": source["generated_manifest"]["sha256"],
        "schema_set": schema_set_sha256,
    })
    if source.get("input_tree_sha256") != expected_input_tree:
        raise RuntimeError("release manifest input tree identity does not match bundled inputs")

    descriptor = metadata["descriptor"]
    generated_manifest = metadata["generated_manifest"]
    if (
        artifact.get("name") != str(descriptor.get('public_name', '')).replace('_', '-')
        or generated_manifest.get("artifact") != artifact
    ):
        raise RuntimeError("release descriptor, generated manifest, and artifact identities differ")
    generated_descriptor = generated_manifest.get("descriptor")
    if (
        not isinstance(generated_descriptor, dict)
        or generated_descriptor.get("sha256") != source["descriptor"]["sha256"]
    ):
        raise RuntimeError("generated SDK manifest does not identify the bundled descriptor")

    generation = generated_manifest.get("generation")
    generator = generation.get("flowdata_sdk") if isinstance(generation, dict) else None
    if not isinstance(generator, dict) or manifest_generator != generator:
        raise RuntimeError("release generator identity does not match generation receipt")

    _verify_descriptor_flatbuffers_locks(
        descriptor, manifest_flatbuffers, bundle_root
    )
    return descriptor


def _descriptor_flatbuffers_locks(
    descriptor: dict[str, object], manifest_flatbuffers: object
) -> tuple[dict[str, object], dict[str, object], dict[str, object]]:
    descriptor_flatbuffers = descriptor.get("flatbuffers")
    descriptor_typescript = descriptor.get("typescript_build")
    if not all(
        isinstance(value, dict)
        for value in (descriptor_flatbuffers, descriptor_typescript, manifest_flatbuffers)
    ):
        raise RuntimeError("release descriptor FlatBuffers metadata is malformed")
    descriptor_flatbuffers = cast(dict[str, object], descriptor_flatbuffers)
    descriptor_typescript = cast(dict[str, object], descriptor_typescript)
    manifest_flatbuffers = cast(dict[str, object], manifest_flatbuffers)
    return descriptor_flatbuffers, descriptor_typescript, manifest_flatbuffers


def _flatbuffers_lock_values(
    descriptor_flatbuffers: dict[str, object],
    descriptor_typescript: dict[str, object],
) -> tuple[str, dict[str, object], dict[str, object], list[object], dict[str, object]]:
    version = descriptor_flatbuffers.get("version")
    python_lock = descriptor_flatbuffers.get("python_wheel")
    source_lock = descriptor_flatbuffers.get("source_archive")
    rust_locks = descriptor_flatbuffers.get("rust_crates")
    typescript_lock = descriptor_typescript.get("flatbuffers_runtime")
    if not isinstance(version, str) or not all(
        isinstance(value, dict)
        for value in (python_lock, source_lock, typescript_lock)
    ) or not isinstance(rust_locks, list):
        raise RuntimeError("release descriptor FlatBuffers locks are malformed")
    python_lock = cast(dict[str, object], python_lock)
    source_lock = cast(dict[str, object], source_lock)
    typescript_lock = cast(dict[str, object], typescript_lock)
    return version, python_lock, source_lock, rust_locks, typescript_lock


def _verify_descriptor_artifact_lock(
    key: str,
    expected: dict[str, object],
    manifest_flatbuffers: dict[str, object],
    bundle_root: Path,
) -> None:
    record = manifest_flatbuffers.get(key)
    if not isinstance(record, dict) or any(
        record.get(field) != value for field, value in expected.items()
    ):
        raise RuntimeError(
            f"release descriptor FlatBuffers {key} lock does not match manifest"
        )
    if key == "source_archive":
        return
    path = validate_relative_path(record.get("path"))
    if path.name != expected["filename"] or not (bundle_root / path).is_file():
        raise RuntimeError(
            f"release descriptor FlatBuffers {key} filename does not match package"
        )


def _verify_descriptor_rust_locks(
    rust_locks: list[object],
    manifest_flatbuffers: dict[str, object],
    bundle_root: Path,
) -> None:
    rust_records = manifest_flatbuffers.get("rust_crates")
    if not isinstance(rust_records, list) or len(rust_records) != len(rust_locks):
        raise RuntimeError("release descriptor Rust crate locks do not match manifest")
    for index, (lock, record) in enumerate(zip(rust_locks, rust_records)):
        if not isinstance(lock, dict) or not isinstance(record, dict):
            raise RuntimeError("release descriptor Rust crate lock is malformed")
        expected = {
            key: lock.get(key)
            for key in ("filename", "name", "sha256", "url", "version")
        }
        if any(record.get(field) != value for field, value in expected.items()):
            raise RuntimeError(
                f"release descriptor Rust crate lock {index} does not match manifest"
            )
        path = validate_relative_path(record.get("path"))
        if path.name != expected["filename"] or not (bundle_root / path).is_file():
            raise RuntimeError("release descriptor Rust crate filename does not match package")


def _verify_descriptor_flatbuffers_locks(
    descriptor: dict[str, object],
    manifest_flatbuffers: object,
    bundle_root: Path,
) -> None:
    descriptor_flatbuffers, descriptor_typescript, manifest_flatbuffers = (
        _descriptor_flatbuffers_locks(descriptor, manifest_flatbuffers)
    )
    version, python_lock, source_lock, rust_locks, typescript_lock = (
        _flatbuffers_lock_values(descriptor_flatbuffers, descriptor_typescript)
    )
    compiler = manifest_flatbuffers.get("compiler")
    if not isinstance(compiler, dict) or compiler.get("semantic_version") != version:
        raise RuntimeError("release descriptor FlatBuffers version does not match compiler")

    expected_records = {
        "python_wheel": {
            "filename": python_lock.get("filename"),
            "name": "flatbuffers",
            "sha256": python_lock.get("sha256"),
            "url": python_lock.get("url"),
            "version": version,
        },
        "source_archive": {
            "filename": source_lock.get("filename"),
            "name": "flatbuffers",
            "sha256": source_lock.get("sha256"),
            "url": source_lock.get("url"),
            "version": version,
        },
        "typescript_package": {
            key: typescript_lock.get(key)
            for key in ("filename", "name", "sha256", "url", "version")
        },
    }
    for key, expected in expected_records.items():
        _verify_descriptor_artifact_lock(
            key, expected, manifest_flatbuffers, bundle_root
        )
    _verify_descriptor_rust_locks(rust_locks, manifest_flatbuffers, bundle_root)


def _verify_packaged_artifact_records(
    bundle_root: Path,
    manifest: dict[str, object],
    file_entries: dict[str, dict[str, object]],
) -> None:
    for section in ("perception_wheel", "perception_npm_package"):
        record = manifest.get(section)
        if not isinstance(record, dict):
            raise RuntimeError(f"release manifest {section} is malformed")
        path = bundle_root / validate_relative_path(record.get("path"))
        if not path.is_file() or sha256(path) != record.get("sha256"):
            raise RuntimeError(f"release manifest {section} checksum mismatch")
        if validate_relative_path(record.get("path")).as_posix() not in file_entries:
            raise RuntimeError(f"release manifest {section} is not listed in files")


def verify_bundle(bundle_root: Path) -> None:
    for name in ("LICENSE", "NOTICE", "LICENSING.md"):
        path = bundle_root / name
        if not path.is_file() or not path.stat().st_size:
            raise RuntimeError(f"SDK release licence evidence is missing: {name}")
    manifest_path = bundle_root / MANIFEST_FILENAME
    manifest = load_json(manifest_path)
    expected_fields = {
        "archive", "artifact", "files", "flatbuffers", "generator", "outputs",
        "payloads", "perception_npm_package", "perception_wheel", "postprocessing", "schemas",
        "schema_set_sha256", "source", "tools",
    }
    if set(manifest) != expected_fields:
        raise RuntimeError("open-perception-kit release manifest fields are stale")
    artifact = manifest.get("artifact")
    if not isinstance(artifact, dict) or not isinstance(artifact.get("version"), str):
        raise RuntimeError("release manifest artifact identity is malformed")
    require_semantic_version(artifact["version"])
    file_entries = _verify_manifest_files(bundle_root, manifest_path, manifest.get("files"))
    descriptor = _verify_source_identities(
        bundle_root,
        manifest.get("source"),
        artifact,
        manifest.get("schema_set_sha256"),
        manifest.get("flatbuffers"),
        manifest.get("generator"),
    )
    _verify_packaged_artifact_records(bundle_root, manifest, file_entries)

    verify_manifest_semantics(bundle_root, manifest, file_entries, descriptor)


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


def _verify_release_tools(source: dict[str, object]) -> None:
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


def _verify_schema_semantics(
    bundle_root: Path, manifest: dict[str, object]
) -> tuple[list[object], str]:
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
    return schema_files, schema_digest


def _verify_python_package_identity(
    manifest: dict[str, object], version: object, expected_name: str
) -> str:
    outputs = manifest.get("outputs")
    python_package = outputs.get("python_package") if isinstance(outputs, dict) else None
    if not isinstance(python_package, dict):
        raise RuntimeError("release Python package identity is invalid")
    distribution_name = python_package.get("distribution_name")
    if (
        distribution_name != expected_name
        or python_package.get("import_name") != distribution_name
        or python_package.get("version") != version
    ):
        raise RuntimeError("release Python package identity is invalid")
    return distribution_name


def _verify_python_packages(
    bundle_root: Path,
    manifest: dict[str, object],
    file_entries: dict[str, dict[str, object]],
    package_version: str,
    public_name: str,
) -> dict[str, object]:
    python_package_name = _verify_python_package_identity(
        manifest, package_version, public_name
    )
    perception = manifest.get("perception_wheel")
    flatbuffers = manifest.get("flatbuffers")
    if not isinstance(perception, dict) or not isinstance(flatbuffers, dict):
        raise RuntimeError("release wheel metadata is missing")
    perception_path = validate_relative_path(perception.get("path"))
    if (
        perception.get("filename") != perception_path.name
        or perception_path.name != f"{public_name}-{package_version}-py3-none-any.whl"
    ):
        raise RuntimeError("release open-perception-kit wheel filename does not match SDK descriptor")
    flatbuffers_wheel = flatbuffers.get("python_wheel")
    if not isinstance(flatbuffers_wheel, dict):
        raise RuntimeError("release FlatBuffers wheel metadata is malformed")
    for label, record, expected_name, expected_version in (
        ("Perception", perception, python_package_name, package_version),
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
            raise RuntimeError("open-perception-kit wheel does not declare FlatBuffers")
    return flatbuffers


def _verify_typescript_packages(
    bundle_root: Path,
    manifest: dict[str, object],
    flatbuffers: dict[str, object],
    file_entries: dict[str, dict[str, object]],
    package_version: str,
    public_name: str,
) -> None:
    perception_npm = manifest.get("perception_npm_package")
    flatbuffers_npm = flatbuffers.get("typescript_package")
    if not isinstance(perception_npm, dict) or not isinstance(flatbuffers_npm, dict):
        raise RuntimeError("release TypeScript package metadata is missing")
    for label, record in (
        ("open-perception-kit TypeScript", perception_npm),
        ("FlatBuffers TypeScript", flatbuffers_npm),
    ):
        relative = validate_relative_path(record.get("path"))
        entry = file_entries.get(relative.as_posix())
        if entry is None or entry.get("sha256") != record.get("sha256"):
            raise RuntimeError(f"release {label} package is inconsistent with files")
    perception_package = npm_package_metadata(
        bundle_root / validate_relative_path(perception_npm["path"])
    )
    if perception_package.get("name") != public_name.replace("_", "-") or perception_package.get("version") != package_version:
        raise RuntimeError("open-perception-kit TypeScript package identity is invalid")
    if perception_package.get("dependencies", {}).get("flatbuffers") != flatbuffers_npm.get("version"):
        raise RuntimeError("open-perception-kit TypeScript package does not declare locked FlatBuffers")
    flatbuffers_package = npm_package_metadata(
        bundle_root / validate_relative_path(flatbuffers_npm["path"])
    )
    if flatbuffers_package.get("name") != "flatbuffers" or flatbuffers_package.get("version") != flatbuffers_npm.get("version"):
        raise RuntimeError("FlatBuffers TypeScript package identity is invalid")


def _rust_release_receipts(
    bundle_root: Path,
    manifest: dict[str, object],
) -> tuple[dict[str, object], dict[str, object], dict[str, object]]:
    outputs = manifest.get("outputs")
    rust_output = outputs.get("rust") if isinstance(outputs, dict) else None
    if not isinstance(rust_output, dict) or rust_output.get("sdk") != "rust":
        raise RuntimeError("release Rust SDK metadata is missing")

    source = manifest.get("source")
    generated_identity = source.get("generated_manifest") if isinstance(source, dict) else None
    if not isinstance(generated_identity, dict):
        raise RuntimeError("release generated SDK identity is missing")
    generated = load_json(
        bundle_root / validate_relative_path(generated_identity.get("path"))
    )
    upstream = generated.get("upstream_receipts")
    rust_receipt = upstream.get("rust") if isinstance(upstream, dict) else None
    if not isinstance(rust_receipt, dict) or rust_receipt.get("outputs") != rust_output:
        raise RuntimeError("release Rust SDK metadata does not match generation receipt")
    return rust_output, rust_receipt, generated


def _verify_rust_runtime_metadata(
    manifest: dict[str, object], rust_receipt: dict[str, object]
) -> None:
    flatbuffers = manifest.get("flatbuffers")
    runtimes = flatbuffers.get("runtimes") if isinstance(flatbuffers, dict) else None
    rust_runtimes = rust_receipt.get("flatbuffers_runtimes")
    if not isinstance(runtimes, list) or not isinstance(rust_runtimes, list) or not all(
        runtime in runtimes for runtime in rust_runtimes
    ):
        raise RuntimeError("release Rust FlatBuffers runtime metadata is missing")


def _rust_generated_files(generated: dict[str, object]) -> list[dict[str, object]]:
    generated_files = generated.get("files")
    if not isinstance(generated_files, list):
        raise RuntimeError("release generated SDK file receipt is missing")
    files = [
        {**record, "path": str(record.get("path", "")).removeprefix("rust/")}
        for record in generated_files
        if isinstance(record, dict) and str(record.get("path", "")).startswith("rust/")
    ]
    if not files:
        raise RuntimeError("release Rust SDK file receipt is missing")
    return files


def _verify_rust_file_receipt(
    bundle_root: Path,
    files: list[dict[str, object]],
    file_entries: dict[str, dict[str, object]],
) -> None:
    expected_paths: set[str] = set()
    for record in files:
        relative = Path("rust") / validate_relative_path(record.get("path"))
        expected_paths.add(relative.as_posix())
        entry = file_entries.get(relative.as_posix())
        path = bundle_root / relative
        if (
            entry is None
            or not path.is_file()
            or entry.get("sha256") != record.get("sha256")
            or entry.get("size") != record.get("size")
        ):
            raise RuntimeError(f"release Rust SDK file does not match: {relative}")
    actual_paths = {
        path.relative_to(bundle_root).as_posix()
        for path in (bundle_root / "rust").rglob("*")
        if path.is_file()
    }
    release_only_paths = {
        path
        for path in actual_paths
        if path == "rust/Cargo.lock"
        or path == "rust/.cargo/config.toml"
        or path.startswith("rust/crates/")
        or path.startswith("rust/vendor/")
    }
    if actual_paths - release_only_paths != expected_paths:
        raise RuntimeError("release Rust SDK file set does not match generation receipt")


def _verify_rust_package_identity(
    bundle_root: Path, artifact: dict[str, object], package_version: str
) -> None:
    cargo_toml = (bundle_root / "rust" / "Cargo.toml").read_text(encoding="utf-8")
    expected_name = str(artifact.get("name", "")).removesuffix("-sdk").replace("-", "_")
    package_section = cargo_toml.split("[dependencies]", 1)[0]
    if (
        re.search(rf'^name\s*=\s*"{re.escape(expected_name)}"\s*$', package_section, re.MULTILINE)
        is None
        or re.search(rf'^version\s*=\s*"{re.escape(package_version)}"\s*$', package_section, re.MULTILINE)
        is None
    ):
        raise RuntimeError("release Rust crate identity is invalid")


def _verify_rust_vendor_record(
    bundle_root: Path,
    record: object,
    file_entries: dict[str, dict[str, object]],
    locked_packages: dict[tuple[str, str], str | None],
) -> str:
    if not isinstance(record, dict):
        raise RuntimeError("release Rust crate artifact is malformed")
    name = record.get("name")
    version = record.get("version")
    checksum = record.get("sha256")
    if not all(isinstance(value, str) for value in (name, version, checksum)):
        raise RuntimeError("release Rust crate artifact identity is malformed")
    name = cast(str, name)
    version = cast(str, version)
    checksum = cast(str, checksum)
    path = validate_relative_path(record.get("path"))
    crate = bundle_root / path
    if (
        not crate.is_file()
        or sha256(crate) != checksum
        or path.as_posix() not in file_entries
    ):
        raise RuntimeError(f"release Rust crate artifact does not match: {path}")
    if locked_packages.get((name, version)) != checksum:
        raise RuntimeError(f"release Rust Cargo lock does not match crate: {name}")
    vendor_relative = Path("rust/vendor") / f"{name}-{version}"
    checksum_metadata = load_json(bundle_root / vendor_relative / ".cargo-checksum.json")
    if checksum_metadata.get("package") != checksum:
        raise RuntimeError(f"release Rust vendor checksum does not match crate: {name}")
    locked_artifact = perception_config.LockedArtifact(
        name=name,
        version=version,
        filename=str(record.get("filename")),
        url=str(record.get("url")),
        sha256=checksum,
    )
    with tempfile.TemporaryDirectory(prefix="perception-rust-vendor-verify-") as tmp:
        expected_vendor = Path(tmp) / f"{name}-{version}"
        extract_rust_crate(crate, expected_vendor, locked_artifact)
        if directory_file_hashes(bundle_root / vendor_relative) != directory_file_hashes(
            expected_vendor
        ):
            raise RuntimeError(f"release Rust vendor contents do not match crate: {name}")
    return vendor_relative.as_posix()


def _verify_rust_vendor(
    bundle_root: Path,
    manifest: dict[str, object],
    file_entries: dict[str, dict[str, object]],
) -> None:
    cargo_config = bundle_root / "rust" / ".cargo" / "config.toml"
    if cargo_config.read_text(encoding="utf-8") != RUST_CARGO_CONFIG:
        raise RuntimeError("release Rust Cargo vendor configuration is invalid")
    locked_packages = cargo_lock_packages(bundle_root / "rust" / "Cargo.lock")
    flatbuffers = manifest.get("flatbuffers")
    rust_records = flatbuffers.get("rust_crates") if isinstance(flatbuffers, dict) else None
    if not isinstance(rust_records, list) or not rust_records:
        raise RuntimeError("release Rust crate artifacts are missing")
    expected_vendor_dirs = {
        _verify_rust_vendor_record(
            bundle_root, record, file_entries, locked_packages
        )
        for record in rust_records
    }
    actual_vendor_dirs = {
        path.relative_to(bundle_root).as_posix()
        for path in (bundle_root / "rust" / "vendor").iterdir()
        if path.is_dir()
    }
    if actual_vendor_dirs != expected_vendor_dirs:
        raise RuntimeError("release Rust vendor directory set is stale")


def _verify_rust_crate(
    bundle_root: Path,
    artifact: dict[str, object],
    manifest: dict[str, object],
    file_entries: dict[str, dict[str, object]],
    package_version: str,
) -> None:
    _, rust_receipt, generated = _rust_release_receipts(bundle_root, manifest)
    _verify_rust_runtime_metadata(manifest, rust_receipt)
    _verify_rust_file_receipt(
        bundle_root, _rust_generated_files(generated), file_entries
    )
    _verify_rust_package_identity(bundle_root, artifact, package_version)
    _verify_rust_vendor(bundle_root, manifest, file_entries)


def verify_manifest_semantics(
    bundle_root: Path,
    manifest: dict[str, object],
    file_entries: dict[str, dict[str, object]],
    descriptor: dict[str, object],
) -> None:
    artifact = manifest["artifact"]
    public_name = descriptor.get("public_name")
    if (
        not isinstance(public_name, str)
        or not isinstance(artifact, dict)
        or artifact.get("name") != public_name.replace("_", "-")
    ):
        raise RuntimeError("release manifest artifact name is invalid")
    source = manifest["source"]
    if not isinstance(source, dict):
        raise RuntimeError("release manifest source metadata is malformed")
    package_prerelease = descriptor.get("package_prerelease", False)
    if not isinstance(package_prerelease, bool):
        raise RuntimeError("release package_prerelease flag is malformed")
    version = str(artifact["version"])
    python_package_version = f"{version}.dev0" if package_prerelease else version
    cargo_package_version = f"{version}-dev.0" if package_prerelease else version
    _verify_release_tools(source)
    _verify_schema_semantics(bundle_root, manifest)
    flatbuffers = _verify_python_packages(
        bundle_root, manifest, file_entries, python_package_version, public_name
    )
    _verify_rust_crate(
        bundle_root, artifact, manifest, file_entries, cargo_package_version
    )
    _verify_typescript_packages(
        bundle_root,
        manifest,
        flatbuffers,
        file_entries,
        cargo_package_version,
        public_name,
    )


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
            raise RuntimeError("open-perception-kit release ZIP entries do not match staged bundle")
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
                raise RuntimeError(f"open-perception-kit release ZIP content mismatch: {info.filename}")


def write_provenance(
    archive_path: Path,
    config: perception_config.SdkConfig,
    generated_manifest_path: Path,
    dirty: bool,
    repository_commit: str,
) -> Path:
    provenance_path = archive_path.with_suffix(archive_path.suffix + PROVENANCE_SUFFIX)
    provenance = {
        "archive": {"path": archive_path.name, "sha256": sha256(archive_path)},
        "descriptor_sha256": config.descriptor_sha256,
        "dirty": dirty,
        "generated_manifest_sha256": sha256(generated_manifest_path),
        "repository_commit": repository_commit,
    }
    provenance_path.write_text(
        json.dumps(provenance, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return provenance_path


def write_checksum(archive_path: Path) -> Path:
    checksum_path = archive_path.with_suffix(archive_path.suffix + CHECKSUM_SUFFIX)
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
    checksum_path = archive_path.with_suffix(archive_path.suffix + CHECKSUM_SUFFIX)
    expected_checksum = f"{sha256(archive_path)}  {archive_path.name}\n"
    if checksum_path.read_text(encoding="utf-8") != expected_checksum:
        raise RuntimeError("release checksum sidecar does not match archive")
    provenance_path = archive_path.with_suffix(archive_path.suffix + PROVENANCE_SUFFIX)
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
            f"expected OPK version {args.expect_version}, product contains {config.version}"
        )
    if args.repository_commit is not None and not GIT_COMMIT_RE.fullmatch(args.repository_commit):
        raise RuntimeError("repository commit must be a full Git SHA")
    generated_manifest = perception_generate.verify_perception_manifest(config)
    if args.repository_commit is not None:
        repository_commit, dirty = args.repository_commit, False
    else:
        repository_commit = git_commit()
        status = repository_git_status()
        dirty = bool(status)
        if dirty and not args.allow_dirty:
            raise RuntimeError(f"SDK inputs or outputs are dirty:\n{status}")

    output_dir = args.output_dir.resolve()
    archive_path = output_dir / f"{config.public_name.replace('_', '-')}-{config.version}.zip"
    with tempfile.TemporaryDirectory(prefix="perception-sdk-release-") as tmp:
        workspace = Path(tmp)
        bundle_root = workspace / f"{config.public_name.replace('_', '-')}-{config.version}"
        shutil.copytree(config.generated_root / "cpp", bundle_root / "cpp")
        for name in perception_generate.SDK_LEGAL_FILES:
            shutil.copy2(config.generated_root / "cpp" / name, bundle_root / name)
        shutil.copy2(REPO_ROOT / "docs/public/licensing.md", bundle_root / "LICENSING.md")
        copy_rust_sdk(config.generated_root / "rust", bundle_root / "rust")
        rust_crates = prepare_rust_vendor(
            rust_root=bundle_root / "rust",
            workspace=workspace,
            config=config,
            artifact_dir=args.artifact_dir,
        )
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
            name=config.public_name, version=config.python_package_version,
        )
        flatbuffers_wheel = acquire_flatbuffers_wheel(
            generated_manifest=generated_manifest, wheel_dir=python_dir,
            supplied_wheel=args.flatbuffers_wheel, config=config,
            artifact_dir=args.artifact_dir,
        )
        typescript_dir = bundle_root / "typescript"
        perception_npm_package = (
            typescript_dir / f"{config.public_name.replace('_', '-')}-{config.cargo_package_version}.tgz"
        )
        write_deterministic_npm_package(config.generated_root / "ts", perception_npm_package)
        flatbuffers_npm_package = acquire_artifact(
            config.typescript_runtime,
            typescript_dir,
            cache_dir=args.artifact_dir,
        )
        write_requirements(python_dir, config)
        write_readme(bundle_root, config)
        write_bundle_manifest(
            bundle_root=bundle_root, config=config, generated_manifest=generated_manifest,
            generated_manifest_path=generated_manifest_path,
            perception_wheel=perception_wheel, flatbuffers_wheel=flatbuffers_wheel,
            perception_npm_package=perception_npm_package,
            flatbuffers_npm_package=flatbuffers_npm_package,
            rust_crates=rust_crates,
        )
        verify_bundle(bundle_root)
        write_deterministic_zip(bundle_root, archive_path)
        verify_zip(bundle_root, archive_path)
    write_provenance(
        archive_path, config, generated_manifest_path, dirty, repository_commit
    )
    write_checksum(archive_path)
    return archive_path


def verify_release_path(path: Path, require_sidecars: bool = False) -> None:
    path = path.resolve()
    if path.is_dir():
        verify_bundle(path)
        return
    verify_release_archive(path)
    checksum = path.with_suffix(path.suffix + CHECKSUM_SUFFIX)
    provenance = path.with_suffix(path.suffix + PROVENANCE_SUFFIX)
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
            "  ./scripts/perception-sdk.sh package --expect-version MAJOR.MINOR.PATCH\n"
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
            "fail unless development/meson.build contains exactly this OPK version; "
            "does not override the product version"
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
        "--repository-commit",
        help="selected repository commit when packaging from a Git-free build context",
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
            "artifacts/open-perception-kit-MAJOR.MINOR.PATCH.zip --require-sidecars"
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
