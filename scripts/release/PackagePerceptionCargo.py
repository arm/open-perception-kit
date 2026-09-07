#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Package the release Perception Rust crate from a verified SDK bundle."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path, PurePosixPath

CRATES_IO_INDEX = "sparse+https://index.crates.io/"
FORBIDDEN_CRATE_DIRECTORIES = {".cargo", "crates", "vendor"}
VERSION_PATTERN = re.compile(r"^\d+\.\d+\.\d+$", re.ASCII)


def fail(message: str) -> None:
    raise RuntimeError(message)


def prepare_manifest(manifest_path: Path, config_path: Path) -> None:
    manifest = manifest_path.read_text(encoding="utf-8")
    manifest, replacements = re.subn(
        r'(?m)^flatbuffers = "(=[^"]+)"$',
        r'flatbuffers = { version = "\1", registry = "crates-io" }',
        manifest,
    )
    if replacements != 1:
        fail("expected one pinned FlatBuffers dependency")
    manifest_path.write_text(manifest, encoding="utf-8")

    config = config_path.read_text(encoding="utf-8")
    if "[registries.crates-io]" in config:
        fail("crates.io registry alias already exists")
    config_path.write_text(
        config
        + "\n[registries.crates-io]\n"
        + f'index = "{CRATES_IO_INDEX}"\n',
        encoding="utf-8",
    )


def extract_sdk(sdk: Path, destination: Path, expected_root: str) -> Path:
    with zipfile.ZipFile(sdk) as archive:
        members = archive.infolist()
        names = [member.filename for member in members]
        if not names or len(names) != len(set(names)):
            fail("Perception SDK archive is empty or contains duplicate paths")
        for member in members:
            path = PurePosixPath(member.filename)
            if path.is_absolute() or ".." in path.parts or not path.parts:
                fail(f"Perception SDK archive contains unsafe path: {member.filename}")
            if path.parts[0] != expected_root:
                fail(f"Perception SDK archive has unexpected root: {member.filename}")
        archive.extractall(destination)
    return destination / expected_root


def verify_crate(crate: Path, version: str) -> None:
    root = f"perception-{version}"
    manifest_name = f"{root}/Cargo.toml"
    with tarfile.open(crate, "r:gz") as archive:  # NOSONAR
        members = archive.getmembers()
        names = [member.name for member in members]
        if not names or len(names) != len(set(names)):
            fail("published crate is empty or contains duplicate paths")
        for member in members:
            path = PurePosixPath(member.name)
            try:
                relative = path.relative_to(root)
            except ValueError:
                fail(f"published crate has unexpected root: {member.name}")
            if relative.parts and relative.parts[0] in FORBIDDEN_CRATE_DIRECTORIES:
                fail(f"published crate contains {relative.parts[0]}")
        try:
            manifest = archive.extractfile(manifest_name)
        except KeyError:
            manifest = None
        if manifest is None:
            fail("published crate has no Cargo.toml")
        cargo_toml = manifest.read().decode("utf-8")
    if f'registry-index = "{CRATES_IO_INDEX}"' not in cargo_toml:
        fail("published FlatBuffers dependency is not pinned to crates.io")


def run_cargo(rust_root: Path) -> None:
    subprocess.run(
        ["cargo", "package", "--offline", "--locked"],
        cwd=rust_root,
        check=True,
    )


def package_sdk(
    *,
    sdk: Path,
    version: str,
    output_dir: Path,
    workspace: Path | None = None,
) -> tuple[Path, Path]:
    if VERSION_PATTERN.fullmatch(version) is None:
        fail("version must use MAJOR.MINOR.PATCH form")
    if not sdk.is_file():
        fail(f"Perception SDK archive does not exist: {sdk}")
    if workspace is not None:
        workspace.mkdir(parents=True, exist_ok=True)

    expected_root = f"perception-sdk-{version}"
    with tempfile.TemporaryDirectory(
        prefix="perception-cargo-package-", dir=workspace
    ) as temporary:
        export_root = extract_sdk(sdk, Path(temporary), expected_root)
        rust_root = export_root / "rust"
        prepare_manifest(
            rust_root / "Cargo.toml", rust_root / ".cargo" / "config.toml"
        )
        shutil.rmtree(rust_root / "crates", ignore_errors=True)
        run_cargo(rust_root)

        crate_name = f"perception-{version}.crate"
        wheel_name = f"opk_perception_sdk-{version}-py3-none-any.whl"
        crate = rust_root / "target" / "package" / crate_name
        wheel = export_root / "python" / wheel_name
        if not crate.is_file():
            fail(f"packaged Perception crate does not exist: {crate}")
        if not wheel.is_file():
            fail(f"Perception wheel does not exist: {wheel}")
        verify_crate(crate, version)

        output_dir.mkdir(parents=True, exist_ok=True)
        output_crate = output_dir / crate_name
        output_wheel = output_dir / wheel_name
        shutil.copy2(crate, output_crate)
        shutil.copy2(wheel, output_wheel)
    return output_crate, output_wheel


def parse_arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--workspace", type=Path)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_arguments(sys.argv[1:] if argv is None else argv)
    try:
        package_sdk(
            sdk=args.sdk.resolve(),
            version=args.version,
            output_dir=args.output_dir.resolve(),
            workspace=args.workspace.resolve() if args.workspace else None,
        )
    except (
        OSError,
        RuntimeError,
        subprocess.CalledProcessError,
        tarfile.TarError,
        zipfile.BadZipFile,
    ) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
