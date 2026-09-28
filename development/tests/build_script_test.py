#!/usr/bin/env python3
################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ARGUMENT = Path(sys.argv[1]) if len(sys.argv) > 1 else None
REPOSITORY_ROOT = (
    REPOSITORY_ARGUMENT.resolve()
    if REPOSITORY_ARGUMENT is not None and REPOSITORY_ARGUMENT.is_dir()
    else Path(__file__).resolve().parents[2]
)
BUILD_SCRIPT_PATH = "scripts/build.sh"
BUILD_SCRIPT = REPOSITORY_ROOT / BUILD_SCRIPT_PATH
SHTOOLS_SCRIPT = REPOSITORY_ROOT / "scripts/private/shtools.sh"
OPK_MENU_PATH = "tools/opk-menu"
NATIVE_BUILD_PATH = "development/build-native"


class BuildScriptTests(unittest.TestCase):
    def make_checkout(self, parent: Path, name: str) -> Path:
        checkout = parent / name
        (checkout / "development").mkdir(parents=True)
        (checkout / "scripts/private").mkdir(parents=True)
        (checkout / "development/meson.build").write_text(
            "project('build-script-test')\n",
            encoding="utf-8",
        )
        shutil.copy2(BUILD_SCRIPT, checkout / BUILD_SCRIPT_PATH)
        shutil.copy2(SHTOOLS_SCRIPT, checkout / "scripts/private/shtools.sh")
        return checkout

    def make_fake_tools(self, parent: Path) -> tuple[Path, Path]:
        bin_dir = parent / "fake-bin"
        bin_dir.mkdir()
        log_path = parent / "meson-invocations.jsonl"

        meson = bin_dir / "meson"
        meson.write_text(
            "#!/usr/bin/env python3\n"
            "import json\n"
            "import os\n"
            "from pathlib import Path\n"
            "import sys\n"
            "\n"
            "with Path(os.environ['OPK_BUILD_SCRIPT_TEST_LOG']).open(\n"
            "    'a', encoding='utf-8'\n"
            ") as output:\n"
            "    output.write(json.dumps({\n"
            "        'argv': sys.argv[1:],\n"
            "        'project_root': os.environ.get('OPK_PROJECT_ROOT'),\n"
            "    }) + '\\n')\n"
            "\n"
            "command = sys.argv[1]\n"
            "if command == 'setup':\n"
            "    (Path(sys.argv[2]) / 'meson-private').mkdir(parents=True)\n"
            "elif command == 'compile':\n"
            "    build_dir = Path(sys.argv[sys.argv.index('-C') + 1])\n"
            "    output_dir = build_dir / 'meson-out'\n"
            "    output_dir.mkdir(parents=True, exist_ok=True)\n"
            "    for artifact in (\n"
            "        'opk-menu', 'opk-config-check', 'libopk-common.so',\n"
            "        'opk-runtime.so', 'opchain-exec', 'pipeline-exec'\n"
            "    ):\n"
            "        (output_dir / artifact).touch()\n",
            encoding="utf-8",
        )
        meson.chmod(0o755)

        ninja = bin_dir / "ninja"
        ninja.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        ninja.chmod(0o755)
        return bin_dir, log_path

    def run_build(
        self,
        checkout: Path,
        bin_dir: Path,
        log_path: Path,
        *arguments: str,
        project_root: Path | str | None = None,
        environment_overrides: dict[str, str] | None = None,
    ) -> subprocess.CompletedProcess[str]:
        environment = os.environ.copy()
        environment["PATH"] = f"{bin_dir}{os.pathsep}{environment['PATH']}"
        environment["OPK_BUILD_SCRIPT_TEST_LOG"] = str(log_path)
        environment["container"] = "opk-build-script-test"
        for variable in (
            "OPK_EXECUTORCH",
            "OPK_PYTHON_OPS",
            "executorch",
            "python_ops",
        ):
            environment.pop(variable, None)
        if project_root is None:
            environment.pop("OPK_PROJECT_ROOT", None)
        else:
            environment["OPK_PROJECT_ROOT"] = str(project_root)
        if environment_overrides:
            environment.update(environment_overrides)

        return subprocess.run(
            [str(checkout / BUILD_SCRIPT_PATH), *arguments],
            cwd=checkout.parent,
            env=environment,
            check=False,
            capture_output=True,
            text=True,
        )

    def read_invocations(self, log_path: Path) -> list[dict[str, object]]:
        return [
            json.loads(line)
            for line in log_path.read_text(encoding="utf-8").splitlines()
        ]

    def test_defaults_to_checkout_containing_build_script(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temporary_root = Path(temporary)
            checkout = self.make_checkout(temporary_root, "checkout with spaces")
            bin_dir, log_path = self.make_fake_tools(temporary_root)

            completed = self.run_build(
                checkout,
                bin_dir,
                log_path,
                "debug",
                "true",
            )

            self.assertEqual(completed.returncode, 0, completed.stderr)
            invocations = self.read_invocations(log_path)
            self.assertEqual(
                invocations[0]["argv"],
                [
                    "setup",
                    str(checkout / NATIVE_BUILD_PATH),
                    str(checkout / "development"),
                    "--buildtype=debug",
                    "-Ddebug=true",
                    "-Dstrip=false",
                    "-Db_lto=false",
                    "-Doptimization=0",
                    "--layout=flat",
                    "-Dtests=true",
                    "-Dexecutorch=auto",
                    "-Dpython_ops=auto",
                ],
            )
            self.assertEqual(
                invocations[1]["argv"],
                ["compile", "-C", str(checkout / NATIVE_BUILD_PATH)],
            )
            self.assertTrue((checkout / OPK_MENU_PATH).is_file())
            self.assertTrue((checkout / "tools/opk-config-check").is_file())
            self.assertTrue((checkout / "tools/opchain-exec").is_file())
            self.assertTrue((checkout / "tools/pipeline-exec").is_file())
            self.assertTrue((checkout / "tools/libopk-common.so").is_file())
            self.assertTrue((checkout / "tools/opk-runtime.so").is_file())
            active_build_dir = checkout / "development/build-active"
            self.assertTrue(active_build_dir.is_symlink())
            self.assertEqual(os.readlink(active_build_dir), "build-native")
            self.assertTrue(
                all(item["project_root"] == str(checkout) for item in invocations)
            )
            self.assertIn(
                "Meson feature selection: executorch=auto",
                completed.stdout,
            )
            self.assertIn(
                f"Pipeline launcher is ready at {checkout / OPK_MENU_PATH}",
                completed.stdout,
            )
            self.assertIn(
                "Run it with:\n  ./scripts/run.sh",
                completed.stdout,
            )

    def test_explicit_backend_selection_is_forwarded_to_meson(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temporary_root = Path(temporary)
            checkout = self.make_checkout(temporary_root, "checkout")
            bin_dir, log_path = self.make_fake_tools(temporary_root)

            completed = self.run_build(
                checkout,
                bin_dir,
                log_path,
                "debug",
                environment_overrides={
                    "OPK_EXECUTORCH": "disabled",
                    "OPK_PYTHON_OPS": "enabled",
                },
            )

            self.assertEqual(completed.returncode, 0, completed.stderr)
            invocations = self.read_invocations(log_path)
            self.assertIn("-Dexecutorch=disabled", invocations[0]["argv"])
            self.assertIn("-Dpython_ops=enabled", invocations[0]["argv"])
            self.assertIn(
                "Meson feature selection: executorch=disabled",
                completed.stdout,
            )
            self.assertIn(
                "Meson feature selection: python_ops=enabled",
                completed.stdout,
            )

    def test_absolute_environment_override_selects_checkout(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temporary_root = Path(temporary)
            driver = self.make_checkout(temporary_root, "driver")
            target = self.make_checkout(temporary_root, "target checkout")
            bin_dir, log_path = self.make_fake_tools(temporary_root)

            completed = self.run_build(
                driver,
                bin_dir,
                log_path,
                "debug",
                project_root=target,
            )

            self.assertEqual(completed.returncode, 0, completed.stderr)
            invocations = self.read_invocations(log_path)
            self.assertEqual(invocations[0]["project_root"], str(target))
            self.assertEqual(invocations[0]["argv"][2], str(target / "development"))
            self.assertTrue((target / OPK_MENU_PATH).is_file())
            self.assertFalse((driver / OPK_MENU_PATH).exists())

    def test_relative_environment_override_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temporary_root = Path(temporary)
            checkout = self.make_checkout(temporary_root, "checkout")
            bin_dir, log_path = self.make_fake_tools(temporary_root)

            completed = self.run_build(
                checkout,
                bin_dir,
                log_path,
                "debug",
                project_root="relative/checkout",
            )

            self.assertEqual(completed.returncode, 2)
            self.assertIn("OPK_PROJECT_ROOT must be an absolute path", completed.stderr)
            self.assertFalse(log_path.exists())

    def test_clean_removes_only_checkout_build_directories(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temporary_root = Path(temporary)
            checkout = self.make_checkout(temporary_root, "checkout")
            bin_dir, log_path = self.make_fake_tools(temporary_root)
            build_dir = checkout / NATIVE_BUILD_PATH
            test_build_dir = checkout / "development/build-native-test"
            container_build_dir = checkout / "development/build"
            build_dir.mkdir()
            test_build_dir.mkdir()
            container_build_dir.mkdir()
            (container_build_dir / "container-artifact").touch()
            active_build_dir = checkout / "development/build-active"
            active_build_dir.symlink_to("build-native", target_is_directory=True)

            completed = self.run_build(
                checkout,
                bin_dir,
                log_path,
                "clean",
            )

            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertFalse(build_dir.exists())
            self.assertFalse(test_build_dir.exists())
            self.assertFalse(active_build_dir.exists())
            self.assertFalse(active_build_dir.is_symlink())
            self.assertTrue((container_build_dir / "container-artifact").is_file())
            self.assertTrue(checkout.is_dir())
            self.assertFalse(log_path.exists())


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
