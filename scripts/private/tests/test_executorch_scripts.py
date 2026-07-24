#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
import unittest


PRIVATE_DIR = Path(__file__).resolve().parents[1]
PROJECT_ROOT = PRIVATE_DIR.parents[1]
EXECUTORCH_DIR = PRIVATE_DIR / "executorch"
PACKAGE_SCRIPT = EXECUTORCH_DIR / "package-executorch-1.3.1-deb.sh"
INSTALL_SCRIPT = EXECUTORCH_DIR / "install-executorch-deb.sh"
SETUP_SCRIPT = EXECUTORCH_DIR / "setup-executorch-1.3.1-deps.sh"
UPLOAD_SCRIPT = EXECUTORCH_DIR / "upload-executorch-1.3.1-deb.sh"
PYTHON_BUILD_REQUIREMENTS = (
    EXECUTORCH_DIR / "python-build-requirements-1.3.1.txt"
)


def write_executable(path: Path, content: str) -> None:
    path.write_text(content, encoding="utf-8")
    path.chmod(0o755)


class ExecuTorchScriptTests(unittest.TestCase):
    def setUp(self):
        test_root = PROJECT_ROOT / "var"
        test_root.mkdir(exist_ok=True)
        self.tempdir = tempfile.TemporaryDirectory(dir=test_root)
        self.addCleanup(self.tempdir.cleanup)
        self.root = Path(self.tempdir.name)
        self.work_dir = self.root / "work"
        self.deps_dir = self.root / "deps"

    def run_script(
        self,
        script: Path,
        *args: str,
        env_updates: dict[str, str] | None = None,
    ) -> subprocess.CompletedProcess[str]:
        env = os.environ.copy()
        for variable in (
            "EXECUTORCH_ARTIFACTORY_COMPONENT",
            "EXECUTORCH_ARTIFACTORY_DISTRIBUTION",
            "EXECUTORCH_ARTIFACTORY_PASSWORD",
            "EXECUTORCH_ARTIFACTORY_PASSWORD_FILE",
            "EXECUTORCH_ARTIFACTORY_REPOSITORY",
            "EXECUTORCH_ARTIFACTORY_SERVER",
            "EXECUTORCH_ARTIFACTORY_USERNAME",
            "EXECUTORCH_ARTIFACTORY_USERNAME_FILE",
            "BUILD_DIR",
            "DEPS_DIR",
            "DOWNLOAD_DIR",
            "EXECUTORCH_DIR",
            "EXECUTORCH_DEB_INSTALL_ROOT",
            "EXECUTORCH_DEB_FETCH_DIR",
            "EXECUTORCH_DEB_PACKAGE_DIR",
            "EXECUTORCH_DEB_ARCHITECTURE",
            "EXECUTORCH_INSTALL_DIR",
            "ARTIFACTORY_FIXTURE_DIR",
            "FAKE_GPG_FINGERPRINT",
            "FAKE_GPG_VERIFY",
            "FAKE_PACKAGE_ARCHITECTURE",
            "FAKE_PACKAGE_NAME",
            "FAKE_PACKAGE_VERSION",
            "FAKE_CURL_LOG",
            "FAKE_REMOTE_SHA256",
            "FAKE_REMOTE_STATUS",
            "VENV_DIR",
        ):
            env.pop(variable, None)
        env.update(env_updates or {})
        return subprocess.run(
            ["bash", str(script), *args],
            check=False,
            capture_output=True,
            text=True,
            env=env,
        )

    def install_tool_environment(
        self, architecture: str = "arm64"
    ) -> dict[str, str]:
        bin_dir = self.root / "bin"
        package_dir = self.root / "packages"
        bin_dir.mkdir(exist_ok=True)
        package_dir.mkdir(exist_ok=True)
        fake_dpkg = bin_dir / "dpkg"
        write_executable(
            fake_dpkg,
            f"""#!/usr/bin/env bash
if [[ "${{1:-}}" == "--print-architecture" ]]; then
    printf '{architecture}\\n'
    exit 0
fi
exit 1
""",
        )
        return {
            "PATH": f"{bin_dir}:{os.environ['PATH']}",
            "EXECUTORCH_DEB_PACKAGE_DIR": str(package_dir),
        }

    def create_artifactory_environment(
        self,
        *,
        architecture: str = "arm64",
        duplicate_package: bool = False,
        tamper_index: bool = False,
        tamper_package: bool = False,
        verify_signature: bool = True,
    ) -> tuple[dict[str, str], bytes]:
        env = self.install_tool_environment(architecture)
        bin_dir = self.root / "bin"
        fixtures = self.root / f"artifactory-{architecture}"
        fetch_dir = self.root / f"fetch-{architecture}"
        fixtures.mkdir()
        package_filename = (
            f"libexecutorch-dev-1.3.1-2-{architecture}.deb"
        )
        package_bytes = b"verified executorch package\n"
        package_path = fixtures / package_filename
        package_path.write_bytes(package_bytes)

        package_sha256 = hashlib.sha256(package_bytes).hexdigest()
        package_stanza = (
            "Package: libexecutorch-dev\n"
            "Version: 1.3.1-2\n"
            f"Architecture: {architecture}\n"
            f"Filename: pool/{package_filename}\n"
            f"Size: {len(package_bytes)}\n"
            f"SHA256: {package_sha256}\n"
        )
        packages = package_stanza
        if duplicate_package:
            packages += f"\n{package_stanza}"
        packages_path = fixtures / "Packages"
        packages_path.write_text(packages, encoding="utf-8")

        packages_bytes = packages_path.read_bytes()
        packages_sha256 = hashlib.sha256(packages_bytes).hexdigest()
        release = (
            "Origin: PEK test repository\n"
            "SHA256:\n"
            f" {packages_sha256} {len(packages_bytes)} "
            f"main/binary-{architecture}/Packages\n"
        )
        (fixtures / "Release").write_text(release, encoding="utf-8")
        (fixtures / "Release.gpg").write_bytes(b"test signature")

        if tamper_index:
            packages_path.write_text(
                f"{packages}\nDescription: tampered\n",
                encoding="utf-8",
            )
        if tamper_package:
            package_path.write_bytes(b"tampered package\n")

        write_executable(
            bin_dir / "curl",
            """#!/usr/bin/env bash
set -euo pipefail
output=""
url=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --netrc-file | --proto | --proto-redir | --output)
            if [[ "$1" == "--output" ]]; then
                output="$2"
            fi
            shift 2
            ;;
        --*)
            shift
            ;;
        *)
            url="$1"
            shift
            ;;
    esac
done
case "${url}" in
    */dists/trixie/Release) source_file="${ARTIFACTORY_FIXTURE_DIR}/Release" ;;
    */dists/trixie/Release.gpg) source_file="${ARTIFACTORY_FIXTURE_DIR}/Release.gpg" ;;
    */dists/trixie/main/binary-*/Packages) source_file="${ARTIFACTORY_FIXTURE_DIR}/Packages" ;;
    */pool/*.deb) source_file="${ARTIFACTORY_FIXTURE_DIR}/${url##*/}" ;;
    *) exit 22 ;;
esac
cp -- "${source_file}" "${output}"
""",
        )
        write_executable(
            bin_dir / "gpg",
            """#!/usr/bin/env bash
set -euo pipefail
arguments=" $* "
if [[ "${arguments}" == *" --import-options show-only "* ]]; then
    cat <<EOF
pub:-:2048:1:EAC1DE22E29E9596:0:0::-:::scSC::::::23::0:
fpr:::::::::${FAKE_GPG_FINGERPRINT}:
EOF
elif [[ "${arguments}" == *" --verify "* ]]; then
    [[ "${FAKE_GPG_VERIFY}" == "1" ]] || exit 1
    printf '[GNUPG:] VALIDSIG %s 2026-07-20 1784555880 0 4 0 1 8 00 %s\\n' \
        "${FAKE_GPG_FINGERPRINT}" "${FAKE_GPG_FINGERPRINT}"
elif [[ "${arguments}" == *" --import "* ]]; then
    exit 0
else
    exit 2
fi
""",
        )
        write_executable(
            bin_dir / "dpkg-deb",
            """#!/usr/bin/env bash
set -euo pipefail
[[ "$1" == "--field" ]]
case "$3" in
    Package) printf '%s\\n' "${FAKE_PACKAGE_NAME}" ;;
    Version) printf '%s\\n' "${FAKE_PACKAGE_VERSION}" ;;
    Architecture) printf '%s\\n' "${FAKE_PACKAGE_ARCHITECTURE}" ;;
    *) exit 2 ;;
esac
""",
        )
        write_executable(
            bin_dir / "sha256sum",
            """#!/usr/bin/env python3
import hashlib
from pathlib import Path
import sys

path = Path(sys.argv[1])
print(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path}")
""",
        )
        write_executable(
            bin_dir / "stat",
            """#!/usr/bin/env python3
from pathlib import Path
import sys

print(Path(sys.argv[-1]).stat().st_size)
""",
        )

        env.update(
            {
                "ARTIFACTORY_FIXTURE_DIR": str(fixtures),
                "EXECUTORCH_ARTIFACTORY_COMPONENT": "main",
                "EXECUTORCH_ARTIFACTORY_DISTRIBUTION": "trixie",
                "EXECUTORCH_ARTIFACTORY_PASSWORD": "test-token",  # pragma: allowlist secret
                "EXECUTORCH_ARTIFACTORY_REPOSITORY": "test-repository",
                "EXECUTORCH_ARTIFACTORY_SERVER": "https://artifactory.example",
                "EXECUTORCH_ARTIFACTORY_USERNAME": "test-user",
                "EXECUTORCH_DEB_FETCH_DIR": str(fetch_dir),
                "EXECUTORCH_REQUIRED": "0",
                "FAKE_GPG_FINGERPRINT": (
                    "190281B95926DE6B8DA2788CEAC1DE22E29E9596"  # pragma: allowlist secret
                ),
                "FAKE_GPG_VERIFY": "1" if verify_signature else "0",
                "FAKE_PACKAGE_ARCHITECTURE": architecture,
                "FAKE_PACKAGE_NAME": "libexecutorch-dev",
                "FAKE_PACKAGE_VERSION": "1.3.1-2",
            }
        )
        return env, package_bytes

    def create_upload_environment(
        self,
        remote_status: str,
        remote_sha256: str = "",
    ) -> tuple[dict[str, str], Path]:
        env = self.install_tool_environment("arm64")
        bin_dir = self.root / "bin"
        package_output = self.root / "upload-package"
        packaged = self.run_script(
            PACKAGE_SCRIPT,
            "--executorch-dir",
            str(self.create_minimal_packageable_sdk()),
            "--output-dir",
            str(package_output),
        )
        if packaged.returncode != 0:
            raise AssertionError(packaged.stderr)
        package_path = next(package_output.glob("*.deb"))
        package_bytes = package_path.read_bytes()
        package_sha256 = hashlib.sha256(package_bytes).hexdigest()
        fixtures = self.root / "upload-artifactory"
        fixtures.mkdir()
        shutil.copy2(package_path, fixtures / package_path.name)
        packages = (
            "Package: libexecutorch-dev\n"
            "Version: 1.3.1-2\n"
            "Architecture: arm64\n"
            f"Filename: pool/{package_path.name}\n"
            f"Size: {len(package_bytes)}\n"
            f"SHA256: {package_sha256}\n"
        )
        packages_path = fixtures / "Packages"
        packages_path.write_text(packages, encoding="utf-8")
        packages_bytes = packages_path.read_bytes()
        (fixtures / "Release").write_text(
            "Origin: PEK upload test repository\n"
            "SHA256:\n"
            f" {hashlib.sha256(packages_bytes).hexdigest()} "
            f"{len(packages_bytes)} main/binary-arm64/Packages\n",
            encoding="utf-8",
        )
        (fixtures / "Release.gpg").write_bytes(b"test signature")
        curl_log = self.root / "upload-curl.log"

        write_executable(
            bin_dir / "sha256sum",
            """#!/usr/bin/env python3
import hashlib
from pathlib import Path
import sys

path = Path(sys.argv[1])
print(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path}")
""",
        )
        write_executable(
            bin_dir / "curl",
            """#!/usr/bin/env bash
set -euo pipefail
printf '%s\\n' "$*" >> "${FAKE_CURL_LOG}"
arguments=" $* "
output=""
url=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --netrc-file | --proto | --proto-redir | --output | --request | --upload-file)
            if [[ "$1" == "--output" ]]; then
                output="$2"
            fi
            shift 2
            ;;
        --*)
            shift
            ;;
        *)
            url="$1"
            shift
            ;;
    esac
done
if [[ "${arguments}" == *" --head "* ]]; then
    if [[ "${FAKE_REMOTE_STATUS}" == "200" ]]; then
        printf 'HTTP/1.1 200\\r\\nX-Checksum-Sha256: %s\\r\\n' \
            "${FAKE_REMOTE_SHA256}" > "${output}"
    else
        printf 'HTTP/1.1 %s\\r\\n' "${FAKE_REMOTE_STATUS}" > "${output}"
    fi
    printf '%s' "${FAKE_REMOTE_STATUS}"
elif [[ "${arguments}" == *" --request PUT "* ]]; then
    printf '{}'
else
    case "${url}" in
        */dists/trixie/Release) source_file="${ARTIFACTORY_FIXTURE_DIR}/Release" ;;
        */dists/trixie/Release.gpg) source_file="${ARTIFACTORY_FIXTURE_DIR}/Release.gpg" ;;
        */dists/trixie/main/binary-arm64/Packages) source_file="${ARTIFACTORY_FIXTURE_DIR}/Packages" ;;
        */pool/*.deb) source_file="${ARTIFACTORY_FIXTURE_DIR}/${url##*/}" ;;
        *) exit 22 ;;
    esac
    cp -- "${source_file}" "${output}"
fi
""",
        )
        write_executable(
            bin_dir / "gpg",
            """#!/usr/bin/env bash
set -euo pipefail
arguments=" $* "
if [[ "${arguments}" == *" --import-options show-only "* ]]; then
    cat <<EOF
pub:-:2048:1:EAC1DE22E29E9596:0:0::-:::scSC::::::23::0:
fpr:::::::::${FAKE_GPG_FINGERPRINT}:
EOF
elif [[ "${arguments}" == *" --verify "* ]]; then
    printf '[GNUPG:] VALIDSIG %s 2026-07-20 1784555880 0 4 0 1 8 00 %s\\n' \
        "${FAKE_GPG_FINGERPRINT}" "${FAKE_GPG_FINGERPRINT}"
elif [[ "${arguments}" == *" --import "* ]]; then
    exit 0
else
    exit 2
fi
""",
        )
        env.update(
            {
                "ARTIFACTORY_FIXTURE_DIR": str(fixtures),
                "EXECUTORCH_ARTIFACTORY_COMPONENT": "main",
                "EXECUTORCH_ARTIFACTORY_DISTRIBUTION": "trixie",
                "EXECUTORCH_ARTIFACTORY_PASSWORD": "test-token",  # pragma: allowlist secret
                "EXECUTORCH_ARTIFACTORY_REPOSITORY": "test-repository",
                "EXECUTORCH_ARTIFACTORY_SERVER": "https://artifactory.example",
                "EXECUTORCH_ARTIFACTORY_USERNAME": "test-user",
                "FAKE_CURL_LOG": str(curl_log),
                "FAKE_GPG_FINGERPRINT": (
                    "190281B95926DE6B8DA2788CEAC1DE22E29E9596"  # pragma: allowlist secret
                ),
                "FAKE_REMOTE_SHA256": remote_sha256,
                "FAKE_REMOTE_STATUS": remote_status,
            }
        )
        return env, package_path

    def create_complete_installed_sdk(self) -> Path:
        install_root = self.root / "installed"
        executorch_root = install_root / "executorch"
        for relative_path in (
            "include/executorch/extension/module/module.h",
            "include/executorch/extension/tensor/tensor_ptr.h",
            "include/executorch/extension/tensor/tensor_ptr_maker.h",
            "include/executorch/runtime/core/error.h",
            "include/executorch/runtime/core/evalue.h",
            "include/executorch/runtime/core/portable_type/c10/c10/util/irange.h",
        ):
            path = executorch_root / relative_path
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        for library in (
            "libextension_module.a",
            "libextension_tensor.a",
            "libextension_flat_tensor.a",
            "libextension_data_loader.a",
            "libextension_named_data_map.a",
            "libextension_threadpool.a",
            "libexecutorch.a",
            "libexecutorch_core.a",
            "libpthreadpool.a",
            "libcpuinfo.a",
            "libportable_ops_lib.a",
            "libportable_kernels.a",
            "libXNNPACK.a",
            "libxnnpack_backend.a",
            "libxnnpack-microkernels-prod.a",
            "libkleidiai.a",
        ):
            path = executorch_root / "lib" / library
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        return install_root

    def create_minimal_packageable_sdk(self) -> Path:
        sdk = self.root / "sdk"
        for relative_path in (
            "include/cpuinfo.h",
            "include/fxdiv.h",
            "include/pthreadpool.h",
            "include/xnnpack.h",
        ):
            path = sdk / relative_path
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        for relative_path in (
            "include/executorch/extension/module/module.h",
            "include/executorch/extension/tensor/tensor_ptr.h",
            "include/executorch/extension/tensor/tensor_ptr_maker.h",
            "include/executorch/runtime/core/error.h",
            "include/executorch/runtime/core/evalue.h",
            "include/executorch/runtime/core/portable_type/c10/c10/util/irange.h",
        ):
            path = sdk / relative_path
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()

        source = self.root / "sdk-object.c"
        object_file = self.root / "sdk-object.o"
        source.write_text("int executorch_test_symbol(void) { return 0; }\n")
        subprocess.run(
            ["cc", "-c", str(source), "-o", str(object_file)],
            check=True,
            capture_output=True,
            text=True,
        )
        library_dir = sdk / "lib"
        library_dir.mkdir()
        for library in (
            "libextension_module.a",
            "libextension_tensor.a",
            "libextension_flat_tensor.a",
            "libextension_data_loader.a",
            "libextension_named_data_map.a",
            "libextension_threadpool.a",
            "libexecutorch.a",
            "libexecutorch_core.a",
            "libpthreadpool.a",
            "libcpuinfo.a",
            "libportable_ops_lib.a",
            "libportable_kernels.a",
            "libXNNPACK.a",
            "libxnnpack_backend.a",
            "libxnnpack-microkernels-prod.a",
        ):
            subprocess.run(
                [
                    "ar",
                    "rcs",
                    str(library_dir / library),
                    str(object_file),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
        if subprocess.run(
            ["dpkg", "--print-architecture"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip() == "arm64":
            (sdk / "include/kai").mkdir()
            subprocess.run(
                [
                    "ar",
                    "rcs",
                    str(library_dir / "libkleidiai.a"),
                    str(object_file),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
        return sdk

    def test_package_install_root_cannot_escape_staging_directory(self):
        output_dir = self.root / "output"

        completed = self.run_script(
            PACKAGE_SCRIPT,
            "--output-dir",
            str(output_dir),
            "--install-root",
            "/../escaped",
        )

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn(
            "--install-root must not contain . or .. path components",
            completed.stderr,
        )
        self.assertFalse((output_dir / "escaped").exists())

    def test_package_validation_rejects_wrong_header_path_type(self):
        sdk = self.create_minimal_packageable_sdk()
        cpuinfo_header = sdk / "include/cpuinfo.h"
        cpuinfo_header.unlink()
        cpuinfo_header.mkdir()

        completed = self.run_script(
            PACKAGE_SCRIPT,
            "--executorch-dir",
            str(sdk),
            "--output-dir",
            str(self.root / "output"),
            "--validate-only",
        )

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn(
            "missing required ExecuTorch top-level header",
            completed.stderr,
        )

    def test_package_validation_rejects_mixed_library_architectures(self):
        sdk = self.create_minimal_packageable_sdk()
        executorch_architecture = subprocess.run(
            ["dpkg", "--print-architecture"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
        wrong_machine = 62 if executorch_architecture == "arm64" else 183
        wrong_object = self.root / "wrong-architecture.o"
        elf_ident = b"\x7fELF" + bytes((2, 1, 1, 0, 0)) + bytes(7)
        wrong_object.write_bytes(
            struct.pack(
                "<16sHHIQQQIHHHHHH",
                elf_ident,
                1,
                wrong_machine,
                1,
                0,
                0,
                0,
                0,
                64,
                0,
                0,
                64,
                0,
                0,
            )
        )
        subprocess.run(
            ["ar", "q", str(sdk / "lib/libcpuinfo.a"), str(wrong_object)],
            check=True,
            capture_output=True,
            text=True,
        )

        completed = self.run_script(
            PACKAGE_SCRIPT,
            "--executorch-dir",
            str(sdk),
            "--output-dir",
            str(self.root / "output"),
            "--expected-architecture",
            executorch_architecture,
            "--validate-only",
        )

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn(
            "archive contains mixed target architectures",
            completed.stderr,
        )

    def test_package_validation_rejects_thin_archives(self):
        sdk = self.create_minimal_packageable_sdk()
        thin_archive = sdk / "lib/libcpuinfo.a"
        thin_archive.unlink()
        subprocess.run(
            [
                "ar",
                "crsT",
                str(thin_archive),
                str(self.root / "sdk-object.o"),
            ],
            check=True,
            capture_output=True,
            text=True,
        )

        completed = self.run_script(
            PACKAGE_SCRIPT,
            "--executorch-dir",
            str(sdk),
            "--output-dir",
            str(self.root / "output"),
            "--validate-only",
        )

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn(
            "library must be a self-contained regular archive",
            completed.stderr,
        )

    @unittest.skipUnless(
        shutil.which("dpkg-deb") and shutil.which("cc"),
        "Debian packaging integration requires the supported devcontainer",
    )
    def test_package_output_is_reproducible_for_identical_sdk(self):
        sdk = self.create_minimal_packageable_sdk()
        first_output = self.root / "first-package"
        second_output = self.root / "second-package"

        first = self.run_script(
            PACKAGE_SCRIPT,
            "--executorch-dir",
            str(sdk),
            "--output-dir",
            str(first_output),
        )
        time.sleep(1.1)
        second = self.run_script(
            PACKAGE_SCRIPT,
            "--executorch-dir",
            str(sdk),
            "--output-dir",
            str(second_output),
        )

        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(second.returncode, 0, second.stderr)
        first_package = next(first_output.glob("*.deb"))
        second_package = next(second_output.glob("*.deb"))
        self.assertEqual(first_package.read_bytes(), second_package.read_bytes())

    def test_required_install_fails_when_no_sdk_source_is_available(self):
        env = self.install_tool_environment()
        env["EXECUTORCH_REQUIRED"] = "1"

        completed = self.run_script(
            INSTALL_SCRIPT,
            env_updates=env,
        )

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("is required but unavailable", completed.stderr)

    def test_optional_install_can_continue_without_an_sdk(self):
        env = self.install_tool_environment()
        env["EXECUTORCH_REQUIRED"] = "0"

        completed = self.run_script(
            INSTALL_SCRIPT,
            env_updates=env,
        )

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn(
            "unavailable; continuing without ExecuTorch support",
            completed.stdout,
        )

    def test_signed_artifactory_chain_stages_the_exact_package(self):
        for architecture in ("arm64", "amd64"):
            with self.subTest(architecture=architecture):
                env, expected_package = self.create_artifactory_environment(
                    architecture=architecture
                )

                completed = self.run_script(INSTALL_SCRIPT, env_updates=env)

                staged_package = (
                    Path(env["EXECUTORCH_DEB_FETCH_DIR"])
                    / f"libexecutorch-dev-1.3.1-2-{architecture}.deb"
                )
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assertEqual(staged_package.read_bytes(), expected_package)

    def test_bad_artifactory_signature_fails_even_when_optional(self):
        env, _ = self.create_artifactory_environment(
            verify_signature=False
        )

        completed = self.run_script(INSTALL_SCRIPT, env_updates=env)

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn(
            "Release signature verification failed",
            completed.stderr,
        )

    def test_signed_index_mismatch_fails_even_when_optional(self):
        env, _ = self.create_artifactory_environment(tamper_index=True)

        completed = self.run_script(INSTALL_SCRIPT, env_updates=env)

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn(
            "Packages index does not match the signed Release",
            completed.stderr,
        )

    def test_package_hash_mismatch_fails_even_when_optional(self):
        env, _ = self.create_artifactory_environment(tamper_package=True)

        completed = self.run_script(INSTALL_SCRIPT, env_updates=env)

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn(
            "package does not match its signed index",
            completed.stderr,
        )

    def test_duplicate_package_record_fails_even_when_optional(self):
        env, _ = self.create_artifactory_environment(
            duplicate_package=True
        )

        completed = self.run_script(INSTALL_SCRIPT, env_updates=env)

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn(
            "Packages index has no unique exact package",
            completed.stderr,
        )

    def test_identical_upload_retry_verifies_signed_readback_without_put(self):
        env, package_path = self.create_upload_environment(
            "200",
        )
        env["FAKE_REMOTE_SHA256"] = hashlib.sha256(
            package_path.read_bytes()
        ).hexdigest()

        completed = self.run_script(
            UPLOAD_SCRIPT,
            str(package_path),
            env_updates=env,
        )

        curl_calls = Path(env["FAKE_CURL_LOG"]).read_text(encoding="utf-8")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("already exists with identical content", completed.stdout)
        self.assertIn("Published and verified", completed.stdout)
        self.assertNotIn("--request PUT", curl_calls)

    def test_upload_refuses_to_overwrite_published_revision(self):
        env, package_path = self.create_upload_environment(
            "200",
            "0" * 64,
        )

        completed = self.run_script(
            UPLOAD_SCRIPT,
            str(package_path),
            env_updates=env,
        )

        curl_calls = Path(env["FAKE_CURL_LOG"]).read_text(encoding="utf-8")
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("refusing to overwrite", completed.stderr)
        self.assertNotIn("--request PUT", curl_calls)

    @unittest.skipUnless(
        shutil.which("dpkg-deb"),
        "Debian packaging integration requires the supported devcontainer",
    )
    def test_upload_rejects_package_maintainer_scripts_before_network(self):
        env, package_path = self.create_upload_environment("404")
        package_root = self.root / "package-with-maintainer-script"
        subprocess.run(
            ["dpkg-deb", "--raw-extract", str(package_path), str(package_root)],
            check=True,
            capture_output=True,
            text=True,
        )
        postinst = package_root / "DEBIAN/postinst"
        postinst.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        postinst.chmod(0o755)
        subprocess.run(
            [
                "dpkg-deb",
                "--root-owner-group",
                "--build",
                str(package_root),
                str(package_path),
            ],
            check=True,
            capture_output=True,
            text=True,
        )

        completed = self.run_script(
            UPLOAD_SCRIPT,
            str(package_path),
            env_updates=env,
        )

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn(
            "control archive must contain only one regular control file",
            completed.stderr,
        )
        self.assertFalse(Path(env["FAKE_CURL_LOG"]).exists())

    @unittest.skipUnless(
        shutil.which("dpkg-deb"),
        "Debian packaging integration requires the supported devcontainer",
    )
    def test_upload_rejects_control_architecture_mismatched_with_payload(self):
        env, package_path = self.create_upload_environment("404")
        package_root = self.root / "package-with-wrong-architecture"
        subprocess.run(
            ["dpkg-deb", "--raw-extract", str(package_path), str(package_root)],
            check=True,
            capture_output=True,
            text=True,
        )
        control_path = package_root / "DEBIAN/control"
        control = control_path.read_text(encoding="utf-8")
        actual_architecture = subprocess.run(
            ["dpkg-deb", "--field", str(package_path), "Architecture"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
        wrong_architecture = (
            "amd64" if actual_architecture == "arm64" else "arm64"
        )
        control_path.write_text(
            control.replace(
                f"Architecture: {actual_architecture}",
                f"Architecture: {wrong_architecture}",
            ),
            encoding="utf-8",
        )
        wrong_package = package_path.with_name(
            package_path.name.replace(actual_architecture, wrong_architecture)
        )
        subprocess.run(
            [
                "dpkg-deb",
                "--root-owner-group",
                "--build",
                str(package_root),
                str(wrong_package),
            ],
            check=True,
            capture_output=True,
            text=True,
        )

        completed = self.run_script(
            UPLOAD_SCRIPT,
            str(wrong_package),
            env_updates=env,
        )

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn(
            f"SDK architecture {actual_architecture} does not match expected "
            f"{wrong_architecture}",
            completed.stderr,
        )
        self.assertFalse(Path(env["FAKE_CURL_LOG"]).exists())

    def test_upload_publishes_only_when_revision_is_absent(self):
        env, package_path = self.create_upload_environment("404")

        completed = self.run_script(
            UPLOAD_SCRIPT,
            str(package_path),
            env_updates=env,
        )

        curl_calls = Path(env["FAKE_CURL_LOG"]).read_text(encoding="utf-8")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("Published and verified", completed.stdout)
        self.assertIn("--request PUT", curl_calls)
        self.assertIn(
            "X-Checksum-Sha256: "
            f"{hashlib.sha256(package_path.read_bytes()).hexdigest()}",
            curl_calls,
        )

    def test_python_build_requirements_are_fully_locked(self):
        logical_lines: list[str] = []
        pending = ""
        for raw_line in PYTHON_BUILD_REQUIREMENTS.read_text(
            encoding="utf-8"
        ).splitlines():
            line = raw_line.split(" # ", 1)[0].strip()
            if not line or line.startswith("#"):
                continue
            pending = f"{pending} {line}".strip()
            if pending.endswith("\\"):
                pending = pending[:-1].strip()
                continue
            logical_lines.append(pending)
            pending = ""

        self.assertFalse(pending)
        expected_contract = {
            (
                "torch",
                "aarch64",
                "download.pytorch.org",
                "4ecd8ecdb9ea1affa5f35d10501809d62dc713f7de9635e8098e760ddbeb852c",  # pragma: allowlist secret
            ),
            (
                "torch",
                "x86_64",
                "download.pytorch.org",
                "d85bdbc271bf22ef1931375a81b0366ab11081509728c58df730cf194a090818",  # pragma: allowlist secret
            ),
            (
                "PyYAML",
                "aarch64",
                "files.pythonhosted.org",
                "42f8152b8dbc4fe7d96729ec2b99c7097d656dc1213a3229ca5383f973a5ed6d",  # pragma: allowlist secret
            ),
            (
                "PyYAML",
                "x86_64",
                "files.pythonhosted.org",
                "d2b04aac4d386b172d5b9692e2d2da8de7bfb6c387fa4f801fbf6fb2e6ba4673",  # pragma: allowlist secret
            ),
            (
                "typing_extensions",
                "",
                "files.pythonhosted.org",
                "a439e7c04b49fec3e5d3e2beaa21755cadbbdc391694e28ccdd36ca4a1408f8c",  # pragma: allowlist secret
            ),
        }
        actual_contract = set()
        for line in logical_lines:
            name, remainder = line.split(" @ ", 1)
            url_and_marker, hash_value = remainder.rsplit(
                " --hash=sha256:", 1
            )
            url, marker = (
                url_and_marker.split(" ; ", 1)
                if " ; " in url_and_marker
                else (url_and_marker, "")
            )
            machine = ""
            if marker:
                prefix = 'platform_machine == "'
                self.assertTrue(marker.startswith(prefix))
                self.assertTrue(marker.endswith('"'))
                machine = marker[len(prefix): -1]
            host = url.split("/", 3)[2]
            actual_contract.add((name, machine, host, hash_value))

        self.assertEqual(actual_contract, expected_contract)

    def test_installed_sdk_check_uses_complete_architecture_contract(self):
        env = self.install_tool_environment()
        install_root = self.create_complete_installed_sdk()
        env["EXECUTORCH_DEB_INSTALL_ROOT"] = str(install_root)

        complete = self.run_script(
            INSTALL_SCRIPT,
            "--check-installed",
            env_updates=env,
        )
        (install_root / "executorch/lib/libkleidiai.a").unlink()
        incomplete = self.run_script(
            INSTALL_SCRIPT,
            "--check-installed",
            env_updates=env,
        )

        self.assertEqual(complete.returncode, 0, complete.stderr)
        self.assertNotEqual(incomplete.returncode, 0)

    def test_installed_sdk_check_does_not_require_kleidiai_on_amd64(self):
        env = self.install_tool_environment("amd64")
        install_root = self.create_complete_installed_sdk()
        (install_root / "executorch/lib/libkleidiai.a").unlink()
        env["EXECUTORCH_DEB_INSTALL_ROOT"] = str(install_root)

        completed = self.run_script(
            INSTALL_SCRIPT,
            "--check-installed",
            env_updates=env,
        )

        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_setup_internal_paths_must_remain_inside_work_directory(self):
        outside_build = self.root / "outside-build"
        outside_build.mkdir()
        sentinel = outside_build / "sentinel"
        sentinel.write_text("keep", encoding="utf-8")

        completed = self.run_script(
            SETUP_SCRIPT,
            str(self.work_dir),
            "--deps-dir",
            str(self.deps_dir),
            env_updates={"BUILD_DIR": str(outside_build)},
        )

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("BUILD_DIR must be inside WORK_DIR", completed.stderr)
        self.assertEqual(sentinel.read_text(encoding="utf-8"), "keep")

    def test_setup_install_paths_must_remain_inside_dependency_directory(self):
        outside_install = self.root / "outside-install"
        outside_install.mkdir()
        sentinel = outside_install / "sentinel"
        sentinel.write_text("keep", encoding="utf-8")

        completed = self.run_script(
            SETUP_SCRIPT,
            str(self.work_dir),
            "--deps-dir",
            str(self.deps_dir),
            env_updates={
                "EXECUTORCH_INSTALL_DIR": str(outside_install),
            },
        )

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn(
            "EXECUTORCH_INSTALL_DIR must be inside DEPS_DIR",
            completed.stderr,
        )
        self.assertEqual(sentinel.read_text(encoding="utf-8"), "keep")

    def test_setup_work_and_dependency_directories_must_not_overlap(self):
        nested_deps = self.work_dir / "deps"

        completed = self.run_script(
            SETUP_SCRIPT,
            str(self.work_dir),
            "--deps-dir",
            str(nested_deps),
            "--keep-work-dir",
        )

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn(
            "WORK_DIR and DEPS_DIR must not overlap",
            completed.stderr,
        )

    def test_setup_roots_must_remain_inside_project(self):
        for argument, value, expected in (
            ("work-dir", "/usr", "WORK_DIR must be inside PROJECT_ROOT"),
            ("deps-dir", "/", "DEPS_DIR must be inside PROJECT_ROOT"),
        ):
            with self.subTest(argument=argument):
                args = (
                    ["/usr", "--deps-dir", str(self.deps_dir)]
                    if argument == "work-dir"
                    else [str(self.work_dir), "--deps-dir", value]
                )
                completed = self.run_script(SETUP_SCRIPT, *args)

                self.assertNotEqual(completed.returncode, 0)
                self.assertIn(expected, completed.stderr)


if __name__ == "__main__":
    unittest.main()
