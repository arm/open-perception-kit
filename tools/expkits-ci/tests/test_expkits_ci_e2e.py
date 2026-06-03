#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
PACKAGE_ROOT = REPO_ROOT / "tools/expkits-ci"
FIXTURE_ROOT = Path(__file__).resolve().parent / "fixtures"


@dataclass(frozen=True)
class FixtureCase:
    target_path: str
    input_fixture: str
    expected_fixture: str


FORMATTER_CASES = (
    FixtureCase("python/good.py", "python/good.py", "python/good.py"),
    FixtureCase("python/bad.py", "python/bad.py.input", "python/bad.py.expected"),
    FixtureCase("src/good.c", "c/good.c", "c/good.c"),
    FixtureCase("src/bad.c", "c/bad.c.input", "c/bad.c.expected"),
    FixtureCase("src/good.cpp", "cpp/good.cpp", "cpp/good.cpp"),
    FixtureCase("src/bad.cpp", "cpp/bad.cpp.input", "cpp/bad.cpp.expected"),
    FixtureCase("include/good.h", "headers/good.h", "headers/good.h"),
    FixtureCase("include/bad.h", "headers/bad.h.input", "headers/bad.h.expected"),
    FixtureCase("include/good.hpp", "headers/good.hpp", "headers/good.hpp"),
    FixtureCase("include/bad.hpp", "headers/bad.hpp.input", "headers/bad.hpp.expected"),
    FixtureCase("cmake/good.cmake", "cmake/good.cmake", "cmake/good.cmake"),
    FixtureCase("cmake/bad.cmake", "cmake/bad.cmake.input", "cmake/bad.cmake.expected"),
    FixtureCase("cmake-good/CMakeLists.txt", "cmake/good.CMakeLists.txt", "cmake/good.CMakeLists.txt"),
    FixtureCase("cmake-bad/CMakeLists.txt", "cmake/bad.CMakeLists.txt.input", "cmake/bad.CMakeLists.txt.expected"),
    FixtureCase("scripts/good.sh", "shell/good.sh", "shell/good.sh"),
    FixtureCase("scripts/bad.sh", "shell/bad.sh.input", "shell/bad.sh.expected"),
)

FAKE_PRIVATE_KEY_FIXTURE = "secrets/fake-private-key.pem"  # pragma: allowlist secret


class TestExpkitsCiE2E(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.test_python = os.environ.get("EXPKITS_CI_TEST_PYTHON", sys.executable)
        cls.runtime_bin_dir = Path(cls.test_python).absolute().parent
        cls.runtime_path = str(cls.runtime_bin_dir) + os.pathsep + os.environ.get("PATH", "")

        runtime_probe = subprocess.run(
            [
                cls.test_python,
                "-c",
                "import argcomplete, git, autopep8, detect_secrets.pre_commit_hook",
            ],
            capture_output=True,
            text=True,
            env={**os.environ, "PATH": cls.runtime_path},
        )
        required_binaries = [
            name for name in ("clang-format", "shfmt", "cmake-format")
            if shutil.which(name, path=cls.runtime_path) is None
        ]
        if runtime_probe.returncode != 0 or required_binaries:
            missing_parts = []
            if runtime_probe.returncode != 0:
                missing_parts.append(
                    "python runtime missing argcomplete/GitPython/autopep8/detect-secrets"
                )
            if required_binaries:
                missing_parts.append(f"missing binaries: {', '.join(required_binaries)}")
            raise unittest.SkipTest("expkits-ci fixture e2e runtime is unavailable; " + "; ".join(missing_parts))

    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempdir.cleanup)
        self.repo_root = Path(self.tempdir.name) / "repo"
        self.repo_root.mkdir()
        self.copy_runtime_inputs()
        self.init_git_repo()

    def runtime_env(self):
        env = os.environ.copy()
        env["PATH"] = self.runtime_path
        env["PYTHONPATH"] = str(PACKAGE_ROOT) + os.pathsep + env.get("PYTHONPATH", "")
        env["HOME"] = str(self.repo_root)
        return env

    def copy_runtime_inputs(self):
        for relative_path in (".clang-format", ".secrets.baseline"):
            source = REPO_ROOT / relative_path
            if source.exists():
                destination = self.repo_root / relative_path
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)

        cmake_format_config = REPO_ROOT / ".cmake-format.yaml"
        destination = self.repo_root / ".cmake-format.yaml"
        destination.parent.mkdir(parents=True, exist_ok=True)
        if cmake_format_config.exists():
            shutil.copy2(cmake_format_config, destination)
        else:
            destination.write_text(self.read_fixture("cmake/cmake-format.yaml"), encoding="utf-8")

        source_header_root = REPO_ROOT / "tools" / "templates" / "header"
        destination_header_root = self.repo_root / "tools" / "templates" / "header"
        destination_header_root.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(source_header_root, destination_header_root, dirs_exist_ok=True)

    def init_git_repo(self):
        self.run_cmd(["git", "init", "-b", "main"], check=True)
        self.run_cmd(["git", "config", "user.name", "expkits-ci E2E"], check=True)
        self.run_cmd(["git", "config", "user.email", "expkits-ci-e2e@example.com"], check=True)

    def run_cmd(self, args, check=False):
        return subprocess.run(
            args,
            cwd=self.repo_root,
            env=self.runtime_env(),
            check=check,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )

    def run_expkits_ci(self, *args):
        return self.run_cmd([self.test_python, "-m", "expkits_ci", *args], check=False)

    def read_fixture(self, relative_path: str) -> str:
        return (FIXTURE_ROOT / relative_path).read_text(encoding="utf-8")

    def write_fixture_files(self, cases: tuple[FixtureCase, ...]) -> None:
        for case in cases:
            destination = self.repo_root / case.target_path
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(self.read_fixture(case.input_fixture), encoding="utf-8")

    def assert_case_matches_expected(self, case: FixtureCase) -> None:
        self.assertEqual(
            (self.repo_root / case.target_path).read_text(encoding="utf-8"),
            self.read_fixture(case.expected_fixture),
            case.target_path,
        )

    def read_detectable_secret_fixture(self) -> str:
        secret_lines = []
        for line in self.read_fixture(FAKE_PRIVATE_KEY_FIXTURE).splitlines():
            if line.startswith("#"):
                continue
            secret_lines.append(line.replace(" # pragma: allowlist secret", ""))

        return "\n".join(secret_lines) + "\n"

    def test_autofix_run_rewrites_fixture_corpus_and_check_run_passes_afterwards(self):
        tracked_paths = [case.target_path for case in FORMATTER_CASES]
        self.write_fixture_files(FORMATTER_CASES)
        report_file = self.repo_root / "artifacts" / "expkits-ci-report.txt"

        first_run = self.run_expkits_ci(
            "--clang-format",
            "--python-format",
            "--cmake-format",
            "--license-header",
            "--shell-format",
            "--report-file",
            str(report_file),
            "--list-of-files",
            *tracked_paths,
        )

        self.assertNotEqual(first_run.returncode, 0, first_run.stdout)
        self.assertIn("Repo checks updated files in place.", first_run.stdout)
        self.assertTrue(report_file.is_file())
        self.assertIn("overall: NOK", report_file.read_text(encoding="utf-8"))
        for case in FORMATTER_CASES:
            self.assert_case_matches_expected(case)

        second_run = self.run_expkits_ci(
            "--clang-format-check",
            "--python-format-check",
            "--cmake-format-check",
            "--license-header-check",
            "--shell-format-check",
            "--list-of-files",
            *tracked_paths,
        )

        self.assertEqual(second_run.returncode, 0, second_run.stdout)
        self.assertIn("[INFO]   OK   clang-format", second_run.stdout)
        self.assertIn("[INFO]   OK   python format", second_run.stdout)
        self.assertIn("[INFO]   OK   cmake format", second_run.stdout)
        self.assertIn("[INFO]   OK   license header", second_run.stdout)
        self.assertIn("[INFO]   OK   shell format", second_run.stdout)
        for case in FORMATTER_CASES:
            self.assert_case_matches_expected(case)

    def test_fixture_based_secret_payload_fails_check_secrets(self):
        secret_path = self.repo_root / "secrets" / "bad.pem"
        secret_path.parent.mkdir(parents=True, exist_ok=True)
        secret_path.write_text(self.read_detectable_secret_fixture(), encoding="utf-8")

        result = self.run_expkits_ci(
            "--check-secrets",
            "--list-of-files",
            "secrets/bad.pem",
        )

        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("Secret Type: Private Key", result.stdout)
        self.assertIn("Location:    secrets/bad.pem:1", result.stdout)
        self.assertIn("[INFO]   NOK  secrets", result.stdout)


if __name__ == "__main__":
    unittest.main()
