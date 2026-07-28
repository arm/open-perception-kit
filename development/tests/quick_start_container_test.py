#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
START_CONTAINER = REPO_ROOT / "scripts/quick-start/start-container.sh"


class QuickStartContainerTests(unittest.TestCase):
    def test_running_container_is_reconciled_for_set_and_unset_token(self):
        for token in (None, "hf_test_token"):
            with self.subTest(token_is_set=token is not None), tempfile.TemporaryDirectory() as root:
                root_path = Path(root)
                bin_path = root_path / "bin"
                bin_path.mkdir()
                log_path = root_path / "docker.log"
                env_path = root_path / ".env"
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
        [[ "$*" != *" uname -m"* ]] || echo x86_64
        exit 0
        ;;
    compose)
        if [[ "$*" == *" up "* ]]; then
            printf 'compose-up token=%s\n' "${HF_TOKEN+set}" >> "$FAKE_DOCKER_LOG"
        fi
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
                        "PATH": f"{bin_path}{os.pathsep}{environment['PATH']}",
                        "PEK_QUICK_START_CI_NAME": "quick-start-auth-test",
                    }
                )
                if token is None:
                    environment.pop("HF_TOKEN", None)
                else:
                    environment["HF_TOKEN"] = token

                completed = subprocess.run(
                    [str(START_CONTAINER)]
                    + (["--env-file", str(env_path)] if token is not None else []),
                    cwd=REPO_ROOT,
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
                self.assertIn(
                    " up -d --no-build --remove-orphans pek-dev\n",
                    log,
                )
                expected_state = "set" if token is not None else ""
                self.assertIn(f"compose-up token={expected_state}\n", log)
                self.assertNotIn(token or "hf_", log)

    @staticmethod
    def _write_executable(path: Path, content: str) -> None:
        path.write_text(content, encoding="utf-8")
        path.chmod(0o755)


if __name__ == "__main__":
    unittest.main()
