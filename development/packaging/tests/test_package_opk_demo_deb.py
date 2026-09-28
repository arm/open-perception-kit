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
PACKAGE_SCRIPT = REPO_ROOT / "scripts/private/packaging/package-opk-demo-deb.sh"


class PackageOpkDemoDebTests(unittest.TestCase):
    def run_command(
        self, arguments: list[str], *, env: dict[str, str] | None = None
    ) -> subprocess.CompletedProcess[str]:
        try:
            return subprocess.run(
                arguments,
                check=True,
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
        except subprocess.CalledProcessError as error:
            self.fail(
                f"command failed: {arguments!r}\n"
                f"stdout:\n{error.stdout}\nstderr:\n{error.stderr}"
            )

    def test_builds_demo_package_with_launcher_and_content(self) -> None:
        architecture = self.run_command(
            ["dpkg-architecture", "-qDEB_BUILD_ARCH"]
        ).stdout.strip()
        multiarch = self.run_command(
            ["dpkg-architecture", "-qDEB_BUILD_MULTIARCH"]
        ).stdout.strip()

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repository = root / "repo"
            build = root / "build"
            install_fixture = root / "install-fixture/usr/bin"
            model = repository / "config/models/example"
            opchains = repository / "config/opchains"
            pipelines = repository / "config/pipelines"
            schemas = repository / "config/schemas"
            images = repository / "data/images"
            videos = repository / "data/videos"
            for directory in (
                build,
                install_fixture,
                model,
                opchains,
                pipelines,
                schemas,
                images,
                videos,
            ):
                directory.mkdir(parents=True)

            source = root / "opk-menu.c"
            source.write_text("int main(void) { return 0; }\n", encoding="utf-8")
            self.run_command(
                [
                    "cc",
                    str(source),
                    f"-Wl,-rpath,$ORIGIN/../lib/{multiarch}/opk",
                    "-o",
                    str(install_fixture / "opk-menu"),
                ]
            )

            (model / "model.onnx").write_bytes(b"model")
            (model / "model.json").write_text(
                json.dumps({"modelFile": "model.onnx"}) + "\n",
                encoding="utf-8",
            )
            (opchains / "example.json").write_text("{}\n", encoding="utf-8")
            (pipelines / "example.json").write_text("{}\n", encoding="utf-8")
            (pipelines / ".last_selected_pipeline_id").write_text(
                "example\n", encoding="utf-8"
            )
            (schemas / "example.json").write_text("{}\n", encoding="utf-8")
            (images / "example.jpg").write_bytes(b"image")
            (videos / "example.mov").write_bytes(b"video")

            fake_meson = root / "meson"
            fake_meson.write_text(
                "#!/usr/bin/env bash\n"
                'cp -a "${OPK_TEST_INSTALL_FIXTURE}/." "${DESTDIR}/"\n',
                encoding="utf-8",
            )
            fake_meson.chmod(0o755)
            output = root / f"opk-demo_0.3.1-1_{architecture}.deb"
            environment = os.environ.copy()
            environment["OPK_TEST_INSTALL_FIXTURE"] = str(
                root / "install-fixture"
            )
            self.run_command(
                [
                    str(PACKAGE_SCRIPT),
                    "--build-dir",
                    str(build),
                    "--meson",
                    str(fake_meson),
                    "--repo-root",
                    str(repository),
                    "--installed-prefix",
                    "/usr",
                    "--version",
                    "0.3.1",
                    "--revision",
                    "1",
                    "--architecture",
                    architecture,
                    "--output",
                    str(output),
                ],
                env=environment,
            )

            extracted = root / "extracted"
            self.run_command(["dpkg-deb", "--extract", str(output), str(extracted)])
            control = self.run_command(["dpkg-deb", "--field", str(output)]).stdout
            self.assertIn("Package: opk-demo", control)
            self.assertIn(f"Architecture: {architecture}", control)
            self.assertIn("Depends: opk-runtime (= 0.3.1-1)", control)
            self.assertTrue((extracted / "usr/bin/opk-menu").is_file())
            self.assertTrue(
                (extracted / "usr/share/opk/config/models/example/model.onnx").is_file()
            )
            self.assertTrue(
                (extracted / "usr/share/opk/data/videos/example.mov").is_file()
            )
            self.assertFalse(
                (
                    extracted
                    / "usr/share/opk/config/pipelines/.last_selected_pipeline_id"
                ).exists()
            )


if __name__ == "__main__":
    unittest.main()
