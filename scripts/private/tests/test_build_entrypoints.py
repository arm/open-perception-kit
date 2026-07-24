#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[3]
BUILD_SCRIPT = REPO_ROOT / "scripts/build-elements.sh"
START_CONTAINER_SCRIPT = REPO_ROOT / "scripts/quick-start/start-container.sh"
SHTOOLS_SCRIPT = REPO_ROOT / "scripts/private/shtools.sh"


def write_executable(path: Path, content: str) -> None:
    path.write_text(content, encoding="utf-8")
    path.chmod(0o755)


class BuildElementsContractTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempdir.cleanup)
        self.root = Path(self.tempdir.name)
        self.scripts_dir = self.root / "scripts"
        self.private_dir = self.scripts_dir / "private"
        self.bin_dir = self.root / "bin"
        self.development_dir = self.root / "development"
        self.tools_dir = self.root / "tools"
        self.private_dir.mkdir(parents=True)
        self.bin_dir.mkdir()
        self.tools_dir.mkdir()
        shutil.copy2(SHTOOLS_SCRIPT, self.private_dir / "shtools.sh")

        script = BUILD_SCRIPT.read_text(encoding="utf-8")
        script = script.replace(
            "PROJECT_ROOT=/work/development",
            f"PROJECT_ROOT={self.development_dir}",
        )
        script = script.replace(
            "PEK_MENU_OUT=/work/tools/pek-menu",
            f"PEK_MENU_OUT={self.tools_dir / 'pek-menu'}",
        )
        script = script.replace(
            "COMMON_LIBRARY_OUT=/work/tools/libcommon.so",
            f"COMMON_LIBRARY_OUT={self.tools_dir / 'libcommon.so'}",
        )
        self.script = self.scripts_dir / "build-elements.sh"
        write_executable(self.script, script)

        self.meson_log = self.root / "meson-calls.jsonl"
        write_executable(
            self.bin_dir / "meson",
            """#!/usr/bin/env python3
import json
import os
from pathlib import Path
import sys

args = sys.argv[1:]
with open(os.environ["FAKE_MESON_LOG"], "a", encoding="utf-8") as log:
    log.write(json.dumps(args) + "\\n")

if args[0] == "setup":
    build_index = 2 if args[1] == "--reconfigure" else 1
    (Path(args[build_index]) / "meson-private").mkdir(parents=True, exist_ok=True)
elif args[0] == "compile":
    build_dir = Path(args[args.index("-C") + 1])
    output_dir = build_dir / "meson-out"
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "pek-menu").write_text("pek-menu", encoding="utf-8")
    (output_dir / "libcommon.so").write_text("libcommon", encoding="utf-8")
""",
        )
        write_executable(self.bin_dir / "ninja", "#!/usr/bin/env bash\nexit 0\n")

    def run_build(
        self, build_type: str, tests: str
    ) -> subprocess.CompletedProcess[str]:
        env = os.environ.copy()
        for variable in (
            "PEK_EXECUTORCH",
            "PEK_HAILORT",
            "PEK_NCNN",
            "executorch",
            "hailort",
            "ncnn",
        ):
            env.pop(variable, None)
        env.update(
            {
                "PATH": f"{self.bin_dir}:{env['PATH']}",
                "FAKE_MESON_LOG": str(self.meson_log),
            }
        )
        return subprocess.run(
            ["bash", str(self.script), build_type, tests],
            check=False,
            capture_output=True,
            text=True,
            env=env,
        )

    def meson_setup_calls(self) -> list[list[str]]:
        return [
            json.loads(line)
            for line in self.meson_log.read_text(encoding="utf-8").splitlines()
            if json.loads(line)[0] == "setup"
        ]

    def test_reconfigure_applies_each_requested_build_contract(self):
        initial_debug = self.run_build("debug", "false")
        debug_with_tests = self.run_build("debug", "true")
        release = self.run_build("release", "false")

        self.assertEqual(initial_debug.returncode, 0, initial_debug.stderr)
        self.assertEqual(debug_with_tests.returncode, 0, debug_with_tests.stderr)
        self.assertEqual(release.returncode, 0, release.stderr)

        calls = self.meson_setup_calls()
        self.assertEqual(len(calls), 3)
        self.assertNotIn("--reconfigure", calls[0])
        self.assertIn("--reconfigure", calls[1])
        self.assertIn("--reconfigure", calls[2])

        build_dir = str(self.development_dir / "build")
        for call in calls:
            self.assertIn(build_dir, call)

        for option in (
            "--buildtype=debug",
            "-Dstrip=false",
            "-Db_lto=false",
            "-Dtests=false",
        ):
            self.assertIn(option, calls[0])

        self.assertIn("-Dtests=true", calls[1])
        for option in (
            "--buildtype=release",
            "-Dstrip=true",
            "-Db_lto=true",
            "-Dtests=false",
        ):
            self.assertIn(option, calls[2])


class QuickStartReuseContractTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempdir.cleanup)
        self.root = Path(self.tempdir.name)
        self.repo = self.root / "repo"
        self.quick_start_dir = self.repo / "scripts/quick-start"
        self.private_dir = self.repo / "scripts/private"
        self.devcontainer_dir = self.repo / ".devcontainer"
        self.bin_dir = self.root / "bin"
        self.quick_start_dir.mkdir(parents=True)
        self.private_dir.mkdir(parents=True)
        self.devcontainer_dir.mkdir()
        self.bin_dir.mkdir()

        self.script = self.quick_start_dir / "start-container.sh"
        shutil.copy2(START_CONTAINER_SCRIPT, self.script)
        write_executable(
            self.quick_start_dir / "detect-environment.sh",
            """#!/usr/bin/env bash
cat <<'EOF'
PEK_PLATFORM_NAME='Test Linux'
PEK_PLATFORM_ID='linux-x86_64'
PEK_CONTAINER_SERVICE='pek-dev-base'
PEK_CONTAINER_NAME='test-pek-container'
PEK_DEV_BASE_CONTAINER_NAME='test-pek-container'
PEK_DEV_RPI5_CONTAINER_NAME='test-pek-rpi5'
PEK_DEV_RPI5_H8_CONTAINER_NAME='test-pek-rpi5-h8'
PEK_DEV_RPI5_H10_CONTAINER_NAME='test-pek-rpi5-h10'
EOF
""",
        )
        write_executable(
            self.private_dir / "detect-webrtc-host-ip.sh",
            "#!/usr/bin/env bash\nprintf '127.0.0.1\\n'\n",
        )
        write_executable(
            self.private_dir / "read-modelfetch-release-manifest.sh",
            "#!/usr/bin/env bash\nprintf 'verified-modelfetch-sdk\\n'\n",
        )
        write_executable(
            self.private_dir / "prepare-modelfetch-release.sh",
            "#!/usr/bin/env bash\nexit 0\n",
        )
        write_executable(
            self.devcontainer_dir / "platform_init.sh",
            "#!/usr/bin/env bash\nexit 0\n",
        )
        (self.private_dir / "modelfetch-release.manifest").write_text(
            "amd64_sha256=verified-modelfetch-sdk\n",
            encoding="utf-8",
        )

        self.docker_log = self.root / "docker-calls.jsonl"
        write_executable(
            self.bin_dir / "docker",
            """#!/usr/bin/env python3
import json
import os
from pathlib import Path
import sys

args = sys.argv[1:]
with open(os.environ["FAKE_DOCKER_LOG"], "a", encoding="utf-8") as log:
    log.write(json.dumps(args) + "\\n")

if args[:2] == ["compose", "version"] or args == ["info"]:
    raise SystemExit(0)
if args and args[0] == "inspect":
    print("true")
    raise SystemExit(0)
if args and args[0] == "exec":
    command = " ".join(args)
    if "test -w /work" in command:
        raise SystemExit(0)
    if args[-2:] == ["uname", "-m"]:
        print("x86_64")
        raise SystemExit(0)
    if ".release-sdk-sha256" in command:
        raise SystemExit(0)
    if "--check-installed" in command:
        raise SystemExit(0 if os.environ["FAKE_EXECUTORCH_PRESENT"] == "1" else 1)
    raise SystemExit(91)
if args and args[0] == "compose":
    if "config" in args and "--environment" in args:
        print(
            "EXECUTORCH_REQUIRED="
            + os.environ["FAKE_EFFECTIVE_EXECUTORCH_REQUIRED"]
        )
        raise SystemExit(0)
    if "up" in args:
        Path(os.environ["FAKE_COMPOSE_UP_MARKER"]).write_text(
            "up", encoding="utf-8"
        )
        raise SystemExit(0)
    raise SystemExit(92)
if args and args[0] == "ps":
    print("test-pek-container Up")
    raise SystemExit(0)
raise SystemExit(93)
""",
        )

    def run_start(
        self, *, required: str, executorch_present: bool
    ) -> tuple[subprocess.CompletedProcess[str], Path]:
        env_file = self.root / "quick-start.env"
        env_file.write_text(
            f"EXECUTORCH_REQUIRED={required}\n",
            encoding="utf-8",
        )
        up_marker = self.root / "compose-up"
        env = os.environ.copy()
        env.pop("EXECUTORCH_REQUIRED", None)
        env.update(
            {
                "PATH": f"{self.bin_dir}:{env['PATH']}",
                "FAKE_DOCKER_LOG": str(self.docker_log),
                "FAKE_COMPOSE_UP_MARKER": str(up_marker),
                "FAKE_EFFECTIVE_EXECUTORCH_REQUIRED": required,
                "FAKE_EXECUTORCH_PRESENT": (
                    "1" if executorch_present else "0"
                ),
            }
        )
        completed = subprocess.run(
            ["bash", str(self.script), "--env-file", str(env_file)],
            check=False,
            capture_output=True,
            text=True,
            env=env,
        )
        return completed, up_marker

    def docker_calls(self) -> list[list[str]]:
        return [
            json.loads(line)
            for line in self.docker_log.read_text(encoding="utf-8").splitlines()
        ]

    def test_required_transition_recreates_running_container_without_sdk(self):
        completed, up_marker = self.run_start(
            required="1",
            executorch_present=False,
        )

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertTrue(up_marker.is_file())
        self.assertIn("missing the current workspace contract", completed.stdout)
        self.assertIn("required ExecuTorch SDK", completed.stdout)

        config_call = next(
            call
            for call in self.docker_calls()
            if "config" in call and "--environment" in call
        )
        self.assertIn("--env-file", config_call)
        up_call = next(call for call in self.docker_calls() if "up" in call)
        self.assertIn("--force-recreate", up_call)

    def test_required_running_container_with_sdk_is_reused(self):
        completed, up_marker = self.run_start(
            required="1",
            executorch_present=True,
        )

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertFalse(up_marker.exists())
        self.assertIn("Container is running: test-pek-container", completed.stdout)
        self.assertFalse(any("up" in call for call in self.docker_calls()))
        self.assertTrue(
            any("--check-installed" in call for call in self.docker_calls())
        )

    def test_optional_running_container_does_not_require_executorch(self):
        completed, up_marker = self.run_start(
            required="0",
            executorch_present=False,
        )

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertFalse(up_marker.exists())
        self.assertIn("Container is running: test-pek-container", completed.stdout)
        self.assertFalse(any("up" in call for call in self.docker_calls()))


if __name__ == "__main__":
    unittest.main()
