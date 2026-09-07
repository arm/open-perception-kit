#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Focused tests for Perception Cargo release packaging."""

import io
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from scripts.release import PackagePerceptionCargo as cargo_tool  # noqa: E402

VERSION = "1.2.3"
CRATE_MANIFEST = f"""\
[package]
name = "perception"
version = "{VERSION}"

[dependencies.flatbuffers]
version = "=25.9.23"
registry-index = "{cargo_tool.CRATES_IO_INDEX}"
"""


def add_tar_file(archive: tarfile.TarFile, name: str, content: bytes) -> None:
    member = tarfile.TarInfo(name)
    member.size = len(content)
    archive.addfile(member, io.BytesIO(content))


def write_crate(path: Path, manifest: str = CRATE_MANIFEST, extra: str | None = None) -> None:
    root = f"perception-{VERSION}"
    path.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(path, "w:gz") as archive:
        add_tar_file(archive, f"{root}/Cargo.toml", manifest.encode())
        add_tar_file(archive, f"{root}/src/lib.rs", b"pub struct Envelope;\n")
        if extra is not None:
            add_tar_file(archive, f"{root}/{extra}/file", b"unexpected\n")


def write_sdk(path: Path) -> None:
    root = f"perception-sdk-{VERSION}"
    files = {
        f"{root}/rust/Cargo.toml": (
            f'[package]\nname = "perception"\nversion = "{VERSION}"\n\n'
            '[dependencies]\nflatbuffers = "=25.9.23"\n'
        ),
        f"{root}/rust/.cargo/config.toml": (
            '[source.crates-io]\nreplace-with = "vendor"\n'
        ),
        f"{root}/rust/crates/flatbuffers.crate": "dependency archive\n",
        f"{root}/rust/src/lib.rs": "pub struct Envelope;\n",
        f"{root}/python/opk_perception_sdk-{VERSION}-py3-none-any.whl": "wheel\n",
    }
    with zipfile.ZipFile(path, "w") as archive:
        for name, content in files.items():
            archive.writestr(name, content)


class PackagePerceptionCargoTests(unittest.TestCase):
    def test_prepare_manifest_assigns_flatbuffers_to_crates_io(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = root / "Cargo.toml"
            config = root / "config.toml"
            manifest.write_text(
                '[dependencies]\nflatbuffers = "=25.9.23"\n', encoding="utf-8"
            )
            config.write_text("[net]\noffline = true\n", encoding="utf-8")

            cargo_tool.prepare_manifest(manifest, config)

            self.assertIn(
                'flatbuffers = { version = "=25.9.23", registry = "crates-io" }',
                manifest.read_text(encoding="utf-8"),
            )
            self.assertIn(
                f'index = "{cargo_tool.CRATES_IO_INDEX}"',
                config.read_text(encoding="utf-8"),
            )

    def test_prepare_manifest_rejects_unexpected_dependency(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = root / "Cargo.toml"
            config = root / "config.toml"
            manifest.write_text(
                '[dependencies]\nflatbuffers = "25.9.23"\n', encoding="utf-8"
            )
            config.write_text("[net]\noffline = true\n", encoding="utf-8")

            with self.assertRaisesRegex(
                RuntimeError, "expected one pinned FlatBuffers dependency"
            ):
                cargo_tool.prepare_manifest(manifest, config)

    def test_verify_crate_rejects_release_only_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            crate = Path(temporary) / f"perception-{VERSION}.crate"
            write_crate(crate, extra="vendor")

            with self.assertRaisesRegex(RuntimeError, "published crate contains vendor"):
                cargo_tool.verify_crate(crate, VERSION)

    def test_verify_crate_rejects_missing_registry(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            crate = Path(temporary) / f"perception-{VERSION}.crate"
            write_crate(
                crate,
                manifest=(
                    '[package]\nname = "perception"\n'
                    f'version = "{VERSION}"\n\n'
                    '[dependencies.flatbuffers]\nversion = "=25.9.23"\n'
                ),
            )

            with self.assertRaisesRegex(
                RuntimeError, "FlatBuffers dependency is not pinned to crates.io"
            ):
                cargo_tool.verify_crate(crate, VERSION)

    def test_package_sdk_stages_verified_language_packages(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdk = root / f"perception-sdk-{VERSION}.zip"
            output = root / "output"
            workspace = root / "workspace"
            write_sdk(sdk)

            def package(rust_root: Path) -> None:
                self.assertFalse((rust_root / "crates").exists())
                self.assertIn(
                    'registry = "crates-io"',
                    (rust_root / "Cargo.toml").read_text(encoding="utf-8"),
                )
                write_crate(
                    rust_root / "target/package" / f"perception-{VERSION}.crate"
                )

            with patch.object(cargo_tool, "run_cargo", side_effect=package):
                crate, wheel = cargo_tool.package_sdk(
                    sdk=sdk,
                    version=VERSION,
                    output_dir=output,
                    workspace=workspace,
                )

            self.assertEqual(crate, output / f"perception-{VERSION}.crate")
            self.assertEqual(
                wheel, output / f"opk_perception_sdk-{VERSION}-py3-none-any.whl"
            )
            self.assertTrue(crate.is_file())
            self.assertEqual(wheel.read_text(encoding="utf-8"), "wheel\n")

    def test_run_cargo_verifies_packaged_archive(self) -> None:
        with patch.object(subprocess, "run") as run:
            cargo_tool.run_cargo(Path("rust"))

        run.assert_called_once_with(
            ["cargo", "package", "--offline", "--locked"],
            cwd=Path("rust"),
            check=True,
        )


if __name__ == "__main__":
    unittest.main()
