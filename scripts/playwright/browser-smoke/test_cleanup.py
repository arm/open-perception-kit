#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


class CleanupStaleContainersTest(unittest.TestCase):
    def test_ignores_disappearing_container_but_not_persistent_failure(self):
        script = Path(__file__).with_name("run.sh")

        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            docker_log = tmp_path / "docker.log"
            docker_ps_seen = tmp_path / "docker-ps-seen"
            fake_docker = tmp_path / "docker"
            fake_docker.write_text(
                "#!/usr/bin/env bash\n"
                "printf '%s\\n' \"$*\" >> \"$DOCKER_LOG\"\n"
                "if [ \"${1:-}\" = ps ] && "
                "{ [ \"${DOCKER_PS_PERSIST:-}\" = true ] || [ ! -e \"$DOCKER_PS_SEEN\" ]; }; then\n"
                "    touch \"$DOCKER_PS_SEEN\"\n"
                "    printf 'stale-one\\nstale-two\\n'\n"
                "elif [ \"${1:-}\" = rm ]; then\n"
                "    exit 1\n"
                "fi\n"
            )
            fake_docker.chmod(0o755)

            env = os.environ.copy()
            env.update(
                {
                    "CI": "true",
                    "DOCKER_LOG": str(docker_log),
                    "DOCKER_PS_SEEN": str(docker_ps_seen),
                    "GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge",
                    "PATH": f"{tmp}:{env['PATH']}",
                    "RUNNER_NAME": "rpi5-272c97",
                }
            )
            subprocess.run([script, "--cleanup-stale"], check=True, env=env)

            self.assertEqual(
                docker_log.read_text().splitlines(),
                [
                    "version",
                    "info",
                    "ps -aq --filter label=com.arm.amp-dev-forge.browser-smoke=true "
                    "--filter label=com.arm.amp-dev-forge.browser-smoke.runner=rpi5-272c97 "
                    "--filter label=com.arm.amp-dev-forge.browser-smoke.repository=Arm-Debug/amp-dev-forge",
                    "rm -f stale-one stale-two",
                    "ps -aq --filter label=com.arm.amp-dev-forge.browser-smoke=true "
                    "--filter label=com.arm.amp-dev-forge.browser-smoke.runner=rpi5-272c97 "
                    "--filter label=com.arm.amp-dev-forge.browser-smoke.repository=Arm-Debug/amp-dev-forge",
                ],
            )

            env["DOCKER_PS_PERSIST"] = "true"
            result = subprocess.run(
                [script, "--cleanup-stale"], capture_output=True, env=env, text=True
            )
            self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
