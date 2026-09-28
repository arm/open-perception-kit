#!/usr/bin/env python3
################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################

import os
import re
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
SETUP_SCRIPT = REPOSITORY_ROOT / "scripts/setup-lxc-dev.sh"
ENTRYPOINT_PATTERN = re.compile(r"scripts/[A-Za-z0-9_./-]+\.(?:py|sh)")


class SetupLxcDevScriptTests(unittest.TestCase):
    def test_early_subprocess_does_not_inherit_hf_token(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            probe = Path(temporary_directory) / "cat"
            probe.write_text(
                "#!/usr/bin/env bash\n"
                "[[ ! -v HF_TOKEN && ! -v hf_token ]] || exit 97\n"
                "/bin/cat\n"
            )
            probe.chmod(0o755)

            environment = os.environ.copy()
            environment["HF_TOKEN"] = "setup-test-token"
            environment["hf_token"] = "preexisting-exported-value"
            environment["PATH"] = (
                f"{temporary_directory}:{environment['PATH']}"
            )
            result = subprocess.run(
                [SETUP_SCRIPT, "--help"],
                capture_output=True,
                check=False,
                env=environment,
                text=True,
            )

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_hf_token_is_scoped_to_model_download(self) -> None:
        setup_script = SETUP_SCRIPT.read_text()
        self.assertIn(
            'hf_token="${HF_TOKEN-}"\nexport -n hf_token\nunset HF_TOKEN',
            setup_script,
        )
        self.assertEqual(setup_script.count('HF_TOKEN="$hf_token"'), 1)
        self.assertRegex(
            setup_script,
            re.compile(
                r'HF_TOKEN="\$hf_token"\s*\\\n'
                r'\s*sudo --preserve-env=OPK_PROJECT_ROOT,HF_TOKEN.*?'
                r'scripts/download-models\.py',
                re.DOTALL,
            ),
        )

    def test_referenced_repository_entrypoints_exist(self) -> None:
        entrypoints = set(ENTRYPOINT_PATTERN.findall(SETUP_SCRIPT.read_text()))
        self.assertTrue(entrypoints)

        missing = sorted(
            entrypoint
            for entrypoint in entrypoints
            if not (REPOSITORY_ROOT / entrypoint).is_file()
        )
        self.assertEqual(missing, [], f"Missing setup entrypoints: {missing}")


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
