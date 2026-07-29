#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
START_CONTAINER = REPO_ROOT / "scripts/quick-start/start-container.sh"


class QuickStartContainerTests(unittest.TestCase):
    def test_running_container_generates_overrides_and_reconciles_token(self):
        cases = (
            (None, None, False),
            ("hf_test_token", "hf_test_token", False),
            ("hf_new_token", "hf_old_token", True),
            (None, "hf_old_token", True),
        )
        for token, container_token, expect_recreate in cases:
            with (
                self.subTest(
                    token_is_set=token is not None,
                    container_token_is_set=container_token is not None,
                ),
                tempfile.TemporaryDirectory() as root,
            ):
                root_path = Path(root)
                fixture_root = root_path / "repo"
                start_container = self._create_fixture(fixture_root)
                bin_path = root_path / "bin"
                bin_path.mkdir()
                log_path = root_path / "docker.log"
                env_path = root_path / "compose.env"
                env_path.touch()
                self._write_executable(
                    bin_path / "uname",
                    """#!/usr/bin/env bash
if [[ "$1" == "-s" ]]; then
    echo Linux
else
    echo x86_64
fi
""",
                )
                self._write_executable(
                    bin_path / "docker",
                    """#!/usr/bin/env bash
printf '%s\n' "$*" >> "$FAKE_DOCKER_LOG"
case "$1" in
    info)
        exit 0
        ;;
    inspect)
        echo true
        exit 0
        ;;
    exec)
        if [[ "$*" == *" uname -m"* ]]; then
            echo x86_64
            exit 0
        fi
        if [[ "$*" == *"cmp -s - /run/secrets/huggingface_token"* ]]; then
            actual="$(cat)"
            if [[ "$actual" == "${FAKE_CONTAINER_TOKEN-}" ]]; then
                exit 0
            fi
            exit 1
        fi
        if [[ "$*" == *"test ! -s /run/secrets/huggingface_token"* ]]; then
            [[ -z "${FAKE_CONTAINER_TOKEN-}" ]]
            exit
        fi
        exit 0
        ;;
    compose)
        if [[ "${2:-}" == "version" ]]; then
            echo 5.3.1
            exit 0
        fi
        if [[ "$*" == *" up "* ]]; then
            arguments=("$@")
            for ((index = 0; index < ${#arguments[@]}; index++)); do
                if [[ "${arguments[index]}" == "-f" ]]; then
                    [[ -f "${arguments[index + 1]}" ]] || exit 7
                fi
            done
            printf 'compose-up token=%s\n' "${HF_TOKEN+set}" >> "$FAKE_DOCKER_LOG"
        fi
        exit 0
        ;;
    ps)
        exit 0
        ;;
esac
exit 1
""",
                )

                environment = os.environ.copy()
                environment.update(
                    {
                        "FAKE_DOCKER_LOG": str(log_path),
                        "FAKE_CONTAINER_TOKEN": container_token or "",
                        "PATH": f"{bin_path}{os.pathsep}{environment['PATH']}",
                        "PEK_QUICK_START_CI_NAME": "quick-start-auth-test",
                    }
                )
                if token is None:
                    environment.pop("HF_TOKEN", None)
                else:
                    environment["HF_TOKEN"] = token

                completed = subprocess.run(
                    [str(start_container)]
                    + (["--env-file", str(env_path)] if token is not None else []),
                    cwd=fixture_root,
                    env=environment,
                    text=True,
                    capture_output=True,
                    check=False,
                )

                log = log_path.read_text(encoding="utf-8")
                self.assertEqual(
                    completed.returncode,
                    0,
                    f"{completed.stderr}\nDocker calls:\n{log}",
                )
                if expect_recreate:
                    self.assertIn(" --force-recreate pek-dev\n", log)
                else:
                    self.assertIn(
                        " up -d --no-build --remove-orphans pek-dev\n",
                        log,
                    )
                self.assertLess(log.index("platform-init\n"), log.index("compose-up token="))
                self.assertIn("compose-up token=set\n", log)
                self.assertNotIn("hf_", log)
                for secret in (token, container_token):
                    if secret is not None:
                        self.assertNotIn(secret, log)

    @classmethod
    def _create_fixture(cls, fixture_root: Path) -> Path:
        quick_start_dir = fixture_root / "scripts/quick-start"
        private_scripts_dir = fixture_root / "scripts/private"
        devcontainer_dir = fixture_root / ".devcontainer"
        quick_start_dir.mkdir(parents=True)
        private_scripts_dir.mkdir(parents=True)
        devcontainer_dir.mkdir()

        start_container = quick_start_dir / "start-container.sh"
        shutil.copy2(START_CONTAINER, start_container)
        (devcontainer_dir / "compose.devcont.yaml").touch()
        cls._write_executable(
            quick_start_dir / "detect-environment.sh",
            """#!/usr/bin/env bash
cat <<'EOF'
PEK_PLATFORM_ID=linux-x86_64
PEK_PLATFORM_NAME='Linux x86_64'
PEK_CONTAINER_SERVICE=pek-dev
PEK_CONTAINER_NAME=quick-start-auth-test
PEK_DEV_CONTAINER_NAME=quick-start-auth-test
PEK_DEV_RPI5_H8_CONTAINER_NAME=quick-start-auth-test-rpi5-h8
PEK_DEV_RPI5_H10_CONTAINER_NAME=quick-start-auth-test-rpi5-h10
PEK_PICAMERA=disabled
EOF
""",
        )
        cls._write_executable(
            private_scripts_dir / "select-webrtc-turn-mode.sh",
            "#!/usr/bin/env bash\necho disabled\n",
        )
        cls._write_executable(
            private_scripts_dir / "read-modelfetch-release-manifest.sh",
            "#!/usr/bin/env bash\nprintf '%064d\\n' 0\n",
        )
        cls._write_executable(
            private_scripts_dir / "prepare-modelfetch-release.sh",
            "#!/usr/bin/env bash\nexit 0\n",
        )
        cls._write_executable(
            private_scripts_dir / "build-dev-base.sh",
            "#!/usr/bin/env bash\nexit 0\n",
        )
        cls._write_executable(
            devcontainer_dir / "platform_init.sh",
            """#!/usr/bin/env bash
printf 'platform-init\n' >> "$FAKE_DOCKER_LOG"
for override in video audio npu shared_memory; do
    touch ".devcontainer/docker-compose.devcont.${override}.yaml"
done
""",
        )
        return start_container

    @staticmethod
    def _write_executable(path: Path, content: str) -> None:
        path.write_text(content, encoding="utf-8")
        path.chmod(0o755)


if __name__ == "__main__":
    unittest.main()
