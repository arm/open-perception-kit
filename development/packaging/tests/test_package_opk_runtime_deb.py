#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[3]
PACKAGE_SCRIPT = (
    REPO_ROOT / "scripts/private/packaging/package-opk-runtime-deb.sh"
)


class PackageOpkRuntimeDebTests(unittest.TestCase):
    def run_command(
        self,
        arguments: list[str],
        *,
        cwd: Path | None = None,
        env: dict[str, str] | None = None,
    ) -> subprocess.CompletedProcess[str]:
        try:
            return subprocess.run(
                arguments,
                check=True,
                cwd=cwd,
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
        except subprocess.CalledProcessError as error:
            self.fail(
                f"command failed: {arguments!r}\n"
                f"stdout:\n{error.stdout}\n"
                f"stderr:\n{error.stderr}"
            )

    def compile_library(
        self,
        output: Path,
        runpath: str,
        soname: str = "",
        link_library: Path | None = None,
    ) -> None:
        source = output.with_suffix(".c")
        if link_library is None:
            source.write_text(
                '#include <stdio.h>\nvoid opk_package_fixture(void) { puts("opk"); }\n',
                encoding="utf-8",
            )
        else:
            source.write_text(
                "void opk_package_fixture(void);\n"
                "void opk_link_fixture(void) { opk_package_fixture(); }\n",
                encoding="utf-8",
            )
        command = [
            "cc",
            "-shared",
            "-fPIC",
            str(source),
            f"-Wl,-rpath,{runpath}",
            "-o",
            str(output),
        ]
        if soname:
            command.insert(-2, f"-Wl,-soname,{soname}")
        if link_library is not None:
            command.insert(-2, str(link_library))
        self.run_command(command)
        source.unlink()

    def test_builds_runtime_package_with_expected_payload(self) -> None:
        architecture = self.run_command(
            ["dpkg-architecture", "-qDEB_BUILD_ARCH"]
        ).stdout.strip()
        multiarch = self.run_command(
            ["dpkg-architecture", "-qDEB_BUILD_MULTIARCH"]
        ).stdout.strip()

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build_dir = root / "build"
            installed_libdir = f"lib/{multiarch}"
            install_fixture = root / "install-fixture/usr" / installed_libdir
            plugin_root = install_fixture / "gstreamer-1.0"
            private_root = install_fixture / "opk"
            python_share = root / "install-fixture/usr/share/opk/python"
            web_root = root / "install-fixture/usr/web/content"
            onnx_license_dir = root / "onnxruntime-licenses"
            executorch_license_dir = root / "executorch-licenses"
            flatbuffers_schema_dir = root / "flatbuffers-schemas"
            open_perception_kit_dir = root / "open-perception-kit"
            repo_root = root / "repo"
            docs_root = repo_root / "docs"
            output = root / f"opk-runtime_0.3.1-1_{architecture}.deb"
            extracted = root / "extracted"
            build_dir.mkdir()
            plugin_root.mkdir(parents=True)
            private_root.mkdir(parents=True)
            python_share.mkdir(parents=True)
            web_root.mkdir(parents=True)
            onnx_license_dir.mkdir()
            (executorch_license_dir / "third-party/example").mkdir(parents=True)
            flatbuffers_schema_dir.mkdir()
            open_perception_kit_dir.mkdir()
            (repo_root / "scripts").mkdir(parents=True)
            (docs_root / "public/how-to").mkdir(parents=True)
            (docs_root / "arch").mkdir()
            (docs_root / "public/how-to/example.md").write_text(
                "# Example\n", encoding="utf-8"
            )
            (docs_root / "arch/example.md").write_text(
                "# Architecture\n", encoding="utf-8"
            )

            for name in (
                "libopkcomm.so",
                "libopkinfer.so",
                "libopkosd.so",
                "libopkperformance.so",
                "libopksink.so",
                "libopktracker.so",
            ):
                self.compile_library(plugin_root / name, "$ORIGIN/../opk")

            onnx_runtime = root / "libonnxruntime.so.1.24.4"
            self.compile_library(
                onnx_runtime,
                "$ORIGIN",
                soname="libonnxruntime.so.1",
            )
            for name in (
                "LICENSE",
                "ThirdPartyNotices.txt",
                "GIT_COMMIT_ID",
                "VERSION_NUMBER",
            ):
                (onnx_license_dir / name).write_text(
                    f"ONNX Runtime {name}\n", encoding="utf-8"
                )
            for name in ("LICENSE", "GIT_COMMIT_ID", "VERSION_NUMBER"):
                (executorch_license_dir / name).write_text(
                    f"ExecuTorch {name}\n", encoding="utf-8"
                )
            (executorch_license_dir / "third-party/example/LICENSE").write_text(
                "Example third-party license\n", encoding="utf-8"
            )
            for name in ("common.fbs", "frame_context.fbs"):
                (flatbuffers_schema_dir / name).write_text(
                    f"// {name}\n", encoding="utf-8"
                )
            for name in (
                "libopk-common.so",
                "opk-runtime.so",
                "opk-executorch-ops.so",
                "opk-python-ops.so",
                "opk-std-ops.so",
            ):
                self.compile_library(private_root / name, "$ORIGIN")
            self.compile_library(
                private_root / "opk-onnx-ops.so",
                "$ORIGIN",
                link_library=onnx_runtime,
            )

            python_extension = root / "numpy-fixture.so"
            python_extension_source = root / "numpy-fixture.cpp"
            python_extension_source.write_text(
                "#include <string>\n"
                "extern \"C\" unsigned long opk_numpy_fixture(void) {\n"
                "    return std::string{\"opk\"}.size();\n"
                "}\n",
                encoding="utf-8",
            )
            self.run_command(
                [
                    "c++",
                    "-shared",
                    "-fPIC",
                    str(python_extension_source),
                    "-o",
                    str(python_extension),
                ]
            )

            verifier = repo_root / "scripts/perception-sdk.sh"
            verifier.write_text("#!/usr/bin/env bash\nexit 0\n", encoding="utf-8")
            verifier.chmod(0o755)
            release_tool = repo_root / "scripts/release/ReleaseTool.py"
            release_tool.parent.mkdir(parents=True)
            release_tool.write_text(
                "import json, os, pathlib, shutil, sys\n"
                "root = pathlib.Path(sys.argv[sys.argv.index('--stage-root') + 1])\n"
                "target = root / 'share/opk/python'\n"
                "target.mkdir(parents=True, exist_ok=True)\n"
                "for module in ('flatbuffers', 'numpy', 'open_perception_kit'):\n"
                "    (target / module).mkdir(exist_ok=True)\n"
                "(target / 'numpy/_core').mkdir()\n"
                "shutil.copy2(os.environ['OPK_TEST_PYTHON_EXTENSION'], "
                "target / 'numpy/_core/_fixture.so')\n"
                "(target / 'opk-runtime.json').write_text(json.dumps({\n"
                "    'distributions': {\n"
                "        'flatbuffers': '25.9.23',\n"
                "        'numpy': '2.4.2',\n"
                "        'open-perception-kit': '0.3.1',\n"
                "    }\n"
                "}) + '\\n')\n",
                encoding="utf-8",
            )
            (python_share / "opk_python_ops.pyi").write_text(
                "", encoding="utf-8"
            )
            (web_root / "index.html").write_text(
                "<!doctype html><title>OPK</title>\n", encoding="utf-8"
            )
            (web_root / "opk-web.js").write_text("", encoding="utf-8")
            self.run_command(["git", "init", "-q"], cwd=repo_root)
            self.run_command(
                [
                    "git",
                    "-c",
                    "user.name=Test",
                    "-c",
                    "user.email=test@example.com",
                    "commit",
                    "--allow-empty",
                    "-qm",
                    "fixture",
                ],
                cwd=repo_root,
            )
            commit = self.run_command(
                ["git", "rev-parse", "HEAD"], cwd=repo_root
            ).stdout.strip()

            archive = open_perception_kit_dir / "open-perception-kit-0.3.1.zip"
            archive.write_bytes(b"fixture")
            (open_perception_kit_dir / f"{archive.name}.sha256").write_text(
                "fixture\n", encoding="utf-8"
            )
            (
                open_perception_kit_dir / f"{archive.name}.provenance.json"
            ).write_text(
                json.dumps({"dirty": False, "repository_commit": commit}) + "\n",
                encoding="utf-8",
            )

            fake_meson = root / "meson"
            fake_meson.write_text(
                "#!/usr/bin/env bash\ncp -a \"${OPK_TEST_INSTALL_FIXTURE}/.\" \"${DESTDIR}/\"\n",
                encoding="utf-8",
            )
            fake_meson.chmod(0o755)

            environment = os.environ.copy()
            environment["OPK_TEST_INSTALL_FIXTURE"] = str(
                root / "install-fixture"
            )
            environment["OPK_TEST_PYTHON_EXTENSION"] = str(python_extension)
            self.run_command(
                [
                    str(PACKAGE_SCRIPT),
                    "--build-dir",
                    str(build_dir),
                    "--meson",
                    str(fake_meson),
                    "--repo-root",
                    str(repo_root),
                    "--installed-prefix",
                    "/usr",
                    "--installed-libdir",
                    installed_libdir,
                    "--python-runtime",
                    os.sys.executable,
                    "--open-perception-kit-dir",
                    str(open_perception_kit_dir),
                    "--onnx-runtime",
                    str(onnx_runtime),
                    "--onnx-license-dir",
                    str(onnx_license_dir),
                    "--executorch-license-dir",
                    str(executorch_license_dir),
                    "--flatbuffers-schema-dir",
                    str(flatbuffers_schema_dir),
                    "--version",
                    "0.3.1",
                    "--revision",
                    "1",
                    "--architecture",
                    architecture,
                    "--multiarch",
                    multiarch,
                    "--output",
                    str(output),
                ],
                env=environment,
            )

            self.assertTrue(output.is_file())
            self.run_command(
                ["dpkg-deb", "--extract", str(output), str(extracted)]
            )
            control = self.run_command(
                ["dpkg-deb", "--field", str(output)]
            ).stdout
            self.assertIn("Package: opk-runtime", control)
            self.assertIn("Version: 0.3.1-1", control)
            self.assertIn(f"Architecture: {architecture}", control)
            self.assertIn("gstreamer1.0-plugins-bad", control)
            self.assertIn("libpython3.13", control)
            self.assertIn("python3.13", control)
            self.assertIn("libstdc++6", control)
            self.assertNotIn("libonnxruntime", control)

            packaged_plugins = {
                path.name
                for path in (
                    extracted / f"usr/lib/{multiarch}/gstreamer-1.0"
                ).iterdir()
            }
            self.assertEqual(
                packaged_plugins,
                {
                    "libopkcomm.so",
                    "libopkinfer.so",
                    "libopkosd.so",
                    "libopkperformance.so",
                    "libopksink.so",
                    "libopktracker.so",
                },
            )
            self.assertTrue(
                (
                    extracted
                    / f"usr/lib/{multiarch}/opk/opk-executorch-ops.so"
                ).is_file()
            )
            self.assertTrue(
                (
                    extracted
                    / f"usr/lib/{multiarch}/opk/opk-runtime.so"
                ).is_file()
            )
            self.assertTrue(
                (
                    extracted
                    / f"usr/lib/{multiarch}/opk/opk-python-ops.so"
                ).is_file()
            )
            self.assertTrue(
                (extracted / "usr/share/opk/python/numpy").is_dir()
            )
            self.assertTrue(
                (
                    extracted
                    / "usr/share/opk/python/numpy/_core/_fixture.so"
                ).is_file()
            )
            self.assertTrue(
                (extracted / "usr/share/opk/python/open_perception_kit").is_dir()
            )
            self.assertTrue(
                (extracted / "usr/share/opk/python/opk_python_ops.pyi").is_file()
            )
            self.assertTrue(
                (extracted / "usr/share/opk/web/index.html").is_file()
            )
            self.assertTrue(
                (extracted / "usr/share/opk/web/opk-web.js").is_file()
            )
            self.assertEqual(
                os.readlink(
                    extracted
                    / f"usr/lib/{multiarch}/opk/libonnxruntime.so.1"
                ),
                "libonnxruntime.so.1.24.4",
            )
            self.assertEqual(
                len(
                    list(
                        (
                            extracted / "usr/share/opk/open-perception-kit"
                        ).iterdir()
                    )
                ),
                3,
            )
            packaged_onnx_files = {
                path.name
                for path in (
                    extracted / "usr/share/opk/licenses/onnxruntime"
                ).iterdir()
            }
            self.assertEqual(
                packaged_onnx_files,
                {
                    "LICENSE",
                    "ThirdPartyNotices.txt",
                    "GIT_COMMIT_ID",
                    "VERSION_NUMBER",
                },
            )
            packaged_executorch_root = (
                extracted / "usr/share/opk/licenses/executorch"
            )
            self.assertTrue((packaged_executorch_root / "LICENSE").is_file())
            self.assertTrue(
                (packaged_executorch_root / "GIT_COMMIT_ID").is_file()
            )
            self.assertTrue(
                (packaged_executorch_root / "VERSION_NUMBER").is_file()
            )
            self.assertTrue(
                (
                    packaged_executorch_root
                    / "third-party/example/LICENSE"
                ).is_file()
            )
            packaged_schemas = {
                path.name
                for path in (
                    extracted / "usr/share/opk/schemas/flatbuffers"
                ).iterdir()
            }
            self.assertEqual(
                packaged_schemas, {"common.fbs", "frame_context.fbs"}
            )
            self.assertTrue(
                (
                    extracted
                    / "usr/share/opk/docs/public/how-to/example.md"
                ).is_file()
            )
            self.assertTrue(
                (extracted / "usr/share/opk/docs/arch/example.md").is_file()
            )


if __name__ == "__main__":
    unittest.main()
