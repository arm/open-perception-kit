#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import json
import os
import re
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
PLATFORM_INIT_PATH = ".devcontainer/platform_init.sh"
CURRENT_IP = "192.0.2.20"
STALE_IP = "192.0.2.10"
COMPOSE_LABEL = "com.docker.compose."

FAKE_DOCKER = r'''#!/usr/bin/env python3
import json
import os
import re
import sys
from pathlib import Path

arguments = sys.argv[1:]
state_path = Path(os.environ["OPK_PLATFORM_INIT_TEST_STATE"])
state = json.loads(state_path.read_text(encoding="utf-8"))
with Path(os.environ["OPK_PLATFORM_INIT_TEST_LOG"]).open("a", encoding="utf-8") as log:
    log.write(json.dumps(arguments) + "\n")
command = arguments[0]
failure = state.get("failure")
if (failure == command and (
    command != "ps" or any(value in arguments for value in ("--all", "-a"))
)) or (
    command == "inspect"
    and ".State.Running" in arguments[arguments.index("--format") + 1]
    and failure == "inspect-running-" + arguments[-1]
):
    print("Simulated Docker " + failure + " failure", file=sys.stderr)
    sys.exit(1)

containers = state["containers"]
if command == "ps":
    filters = [arguments[i + 1] for i, value in enumerate(arguments) if value == "--filter"]
    template = arguments[arguments.index("--format") + 1]
    for container_id, container in containers.items():
        if not container["running"] and not any(value in arguments for value in ("--all", "-a")):
            continue
        selected = True
        for value in filters:
            assert value.startswith("label="), value
            label, separator, expected = value[6:].partition("=")
            actual = container["labels"].get(label)
            if actual is None or (separator and actual != expected):
                selected = False
        if selected:
            output = template.replace("{{.ID}}", container_id)
            output = re.sub(
                r'{{\s*\.Label "([^"]+)"\s*}}',
                lambda match: container["labels"].get(match.group(1), ""),
                output,
            )
            assert "{{" not in output, output
            print(output)
elif command == "inspect":
    template = arguments[arguments.index("--format") + 1]
    container = containers[arguments[-1]]
    if ".Config.Labels" in template:
        label = re.search(r'"([^"]+)"', template).group(1)
        print(container["labels"].get(label, ""))
    elif ".Config.Env" in template:
        print("\n".join(container["env"]))
    elif ".Config.Cmd" in template:
        print("\n".join(container["cmd"]))
    elif ".HostConfig.RestartPolicy.Name" in template:
        print(container["restart"])
    elif ".State.Running" in template:
        print(str(container["running"]).lower())
    else:
        raise AssertionError("Unsupported inspect template: " + template)
elif command in ("rm", "stop", "start", "update"):
    for container_id in (value for value in arguments[1:] if not value.startswith("-")):
        if command == "rm":
            del containers[container_id]
        elif command == "stop":
            containers[container_id]["running"] = False
        elif command == "start":
            containers[container_id]["running"] = True
        else:
            assert "--restart=no" in arguments, arguments
            containers[container_id]["restart"] = "no"
    state_path.write_text(json.dumps(state), encoding="utf-8")
else:
    raise AssertionError("Unsupported Docker command: " + repr(arguments))
'''


class PlatformInitScriptTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.temporary_root = Path(temporary.name)
        self.checkout = self.temporary_root / "checkout with spaces"
        (self.checkout / ".devcontainer").mkdir(parents=True)
        (self.checkout / "scripts/private").mkdir(parents=True)
        shutil.copy2(
            REPOSITORY_ROOT / PLATFORM_INIT_PATH,
            self.checkout / PLATFORM_INIT_PATH,
        )
        for kind in ("devcont", "rich"):
            name = f".devcontainer/docker-compose.{kind}.turn.yaml"
            shutil.copy2(REPOSITORY_ROOT / name, self.checkout / name)

        scripts = {
            "dev-init.sh": "exit 0\n",
            "generate-hf-download-cachebust.sh": "echo test-cachebust\n",
            "select-webrtc-turn-mode.sh": "echo enabled\n",
            "detect-webrtc-host-ip.sh": (
                'echo called >> "$OPK_PLATFORM_INIT_TEST_DETECTOR_LOG"\n'
                f"echo {CURRENT_IP}\n"
            ),
        }
        for name, contents in scripts.items():
            script = self.checkout / "scripts/private" / name
            script.write_text("#!/bin/sh\n" + contents, encoding="utf-8")
            script.chmod(0o755)
        for name in (".env", ".devcontainer/.env", "devices.env"):
            (self.checkout / name).write_text(
                f"WEBRTC_HOST_IP={STALE_IP}\nOPK_TEST_SENTINEL=preserve\n",
                encoding="utf-8",
            )

        self.bin_dir = self.temporary_root / "fake-bin"
        self.bin_dir.mkdir()
        docker = self.bin_dir / "docker"
        docker.write_text(FAKE_DOCKER, encoding="utf-8")
        docker.chmod(0o755)
        self.state_path = self.temporary_root / "docker-state.json"
        self.log_path = self.temporary_root / "docker-invocations.jsonl"
        self.detector_log = self.temporary_root / "detector-invocations"
        workspace_hash = subprocess.run(
            ["git", "hash-object", "--stdin"],
            input=str(self.checkout),
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()[:12]
        self.project = f"opk-dev-{workspace_hash}"

    def container(
        self,
        service: str,
        ip: str | None = STALE_IP,
        *,
        running: bool = True,
        restart: str = "unless-stopped",
        oneoff: bool = False,
        project: str | None = None,
        working_dir: Path | None = None,
        compose_file: str = "compose.devcont.yaml",
    ) -> dict[str, object]:
        return {
            "labels": {
                COMPOSE_LABEL + "project": project or self.project,
                COMPOSE_LABEL + "service": service,
                COMPOSE_LABEL + "oneoff": str(oneoff),
                COMPOSE_LABEL + "project.config_files": str(
                    self.checkout / ".devcontainer" / compose_file
                ),
                COMPOSE_LABEL + "project.working_dir": str(
                    working_dir or self.checkout / ".devcontainer"
                ),
            },
            "running": running,
            "env": ["OPK_TEST_SENTINEL=preserve"]
            + ([f"WEBRTC_HOST_IP={ip}"] if service == "opk-dev" and ip is not None else []),
            "cmd": (
                ["--listening-port=3478"]
                + ([f"--external-ip={ip}"] if ip is not None else [])
                if service == "coturn"
                else ["sleep", "infinity"]
            ),
            "restart": restart,
        }

    def set_containers(
        self,
        containers: dict[str, dict[str, object]],
        *,
        failure: str | None = None,
    ) -> None:
        self.state_path.write_text(
            json.dumps({"containers": containers, "failure": failure}),
            encoding="utf-8",
        )
        self.log_path.unlink(missing_ok=True)

    def run_init(
        self,
        *,
        service: str = "opk-dev",
        turn: str = "enabled",
        kind: str = "devcont",
        cleanup: bool = True,
        explicit_ip: str | None = None,
    ) -> subprocess.CompletedProcess[str]:
        environment = os.environ.copy()
        for name in (
            "WEBRTC_HOST_IP",
            "COMPOSE_PROJECT_NAME",
            "OPK_DEV_CONTAINER_NAME",
            "HF_DOWNLOAD_CACHEBUST",
            "OPK_WEBRTC_TURN_MIN_PORT",
            "OPK_WEBRTC_TURN_MAX_PORT",
        ):
            environment.pop(name, None)
        environment.update(
            {
                "PATH": f"{self.bin_dir}{os.pathsep}{environment['PATH']}",
                "OPK_PLATFORM_INIT_TEST_STATE": str(self.state_path),
                "OPK_PLATFORM_INIT_TEST_LOG": str(self.log_path),
                "OPK_PLATFORM_INIT_TEST_DETECTOR_LOG": str(self.detector_log),
                "OPK_BUILD_BASE_IMAGE": "test-build-base",
            }
        )
        if explicit_ip is not None:
            environment["WEBRTC_HOST_IP"] = explicit_ip
        arguments = [service, "disabled", turn, kind]
        if cleanup:
            arguments.append("stop-other-devcontainers")
        return subprocess.run(
            ["bash", str(self.checkout / PLATFORM_INIT_PATH), *arguments],
            cwd=self.temporary_root,
            env=environment,
            capture_output=True,
            text=True,
            check=False,
        )

    def containers(self) -> dict[str, dict[str, object]]:
        return json.loads(self.state_path.read_text(encoding="utf-8"))["containers"]

    def invocations(self, command: str) -> list[list[str]]:
        if not self.log_path.exists():
            return []
        return [
            arguments
            for line in self.log_path.read_text(encoding="utf-8").splitlines()
            if (arguments := json.loads(line))[0] == command
        ]

    def assert_removed(self, *container_ids: str) -> None:
        removals = self.invocations("rm")
        self.assertEqual(
            sorted(
                value
                for call in removals
                for value in call[1:]
                if not value.startswith("-")
            ),
            sorted(container_ids),
        )
        for call in removals:
            self.assertIn("-f", call)
            self.assertFalse(any("v" in value for value in call[1:] if value.startswith("-")))

    def workspace_restart_policies(self) -> dict[str, str]:
        workspace = self.checkout / ".devcontainer/docker-compose.devcont.workspace.yaml"
        if not workspace.exists():
            return {}
        policies = {}
        service = None
        for line in workspace.read_text(encoding="utf-8").splitlines():
            match = re.fullmatch(r"  ([\w-]+):", line)
            if match:
                service = match.group(1)
            match = re.fullmatch(r"    restart: (.+)", line)
            if match:
                policies[service] = match.group(1).strip("\"'")
        return policies

    def test_removes_stale_running_and_stopped_pairs_without_volumes(self) -> None:
        for running in (True, False):
            with self.subTest(running=running):
                self.set_containers(
                    {
                        "dev-current": self.container("opk-dev", running=running),
                        "turn-current": self.container(
                            "coturn",
                            running=running,
                            working_dir=self.checkout if not running else None,
                        ),
                    }
                )
                completed = self.run_init()
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assert_removed("dev-current", "turn-current")
                self.assertEqual(self.invocations("start"), [])
                self.assertEqual(self.containers(), {})
                for name in (".env", ".devcontainer/.env", "devices.env"):
                    contents = (self.checkout / name).read_text(encoding="utf-8")
                    self.assertIn(f"WEBRTC_HOST_IP={CURRENT_IP}\n", contents)
                    self.assertIn("OPK_TEST_SENTINEL=preserve\n", contents)

    def test_removes_pair_when_only_relay_is_stale_or_an_ip_is_missing(self) -> None:
        for dev_ip, turn_ip in (
            (CURRENT_IP, STALE_IP),
            (None, CURRENT_IP),
            (CURRENT_IP, None),
        ):
            with self.subTest(dev_ip=dev_ip, turn_ip=turn_ip):
                self.set_containers(
                    {
                        "dev-current": self.container("opk-dev", dev_ip),
                        "turn-current": self.container("coturn", turn_ip, running=False),
                    }
                )
                completed = self.run_init()
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assert_removed("dev-current", "turn-current")
                self.assertEqual(self.invocations("start"), [])
                self.assertEqual(self.containers(), {})

    def test_removes_remaining_member_of_partial_pair(self) -> None:
        for service in ("opk-dev", "coturn"):
            with self.subTest(service=service):
                self.set_containers({"remaining": self.container(service, CURRENT_IP)})
                completed = self.run_init()
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assert_removed("remaining")
                self.assertEqual(self.invocations("start"), [])
                self.assertEqual(self.containers(), {})

    def test_no_existing_pair_needs_no_removal_or_policy_update(self) -> None:
        self.set_containers({})
        completed = self.run_init()
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assert_removed()
        self.assertEqual(self.invocations("update"), [])
        self.assertEqual(self.invocations("start"), [])

    def test_oneoff_containers_are_excluded_from_pair_reconciliation(self) -> None:
        for service in ("opk-dev", "coturn"):
            for running, pair_ip, oneoff_ip, removed in (
                (True, CURRENT_IP, STALE_IP, ()),
                (False, CURRENT_IP, STALE_IP, ()),
                (True, STALE_IP, CURRENT_IP, ("dev-current", "turn-current")),
                (False, STALE_IP, CURRENT_IP, ("dev-current", "turn-current")),
            ):
                with self.subTest(service=service, running=running, pair_ip=pair_ip):
                    containers = {
                        "dev-current": self.container("opk-dev", pair_ip, restart="no"),
                        "turn-current": self.container("coturn", pair_ip, restart="no"),
                        "oneoff": self.container(
                            service, oneoff_ip, running=running, oneoff=True
                        ),
                    }
                    self.set_containers(containers)
                    completed = self.run_init()
                    self.assertEqual(completed.returncode, 0, completed.stderr)
                    self.assert_removed(*removed)
                    self.assertEqual(
                        self.containers(),
                        {key: value for key, value in containers.items() if key not in removed},
                    )
                    for command in ("stop", "start", "update"):
                        self.assertEqual(self.invocations(command), [])

    def test_matching_pair_is_kept_and_only_legacy_policies_are_updated(self) -> None:
        for dev_restart, turn_restart, expected_updates in (
            ("no", "no", []),
            ("unless-stopped", "unless-stopped", ["dev-current", "turn-current"]),
            ("no", "unless-stopped", ["turn-current"]),
        ):
            with self.subTest(dev_restart=dev_restart, turn_restart=turn_restart):
                self.set_containers(
                    {
                        "dev-current": self.container(
                            "opk-dev", CURRENT_IP, restart=dev_restart
                        ),
                        "turn-current": self.container(
                            "coturn", CURRENT_IP, restart=turn_restart
                        ),
                    }
                )
                completed = self.run_init()
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assert_removed()
                self.assertEqual(self.invocations("start"), [])
                updates = self.invocations("update")
                self.assertEqual(
                    sorted(
                        value
                        for call in updates
                        for value in call[1:]
                        if not value.startswith("-")
                    ),
                    expected_updates,
                )
                for call in updates:
                    self.assertIn("--restart=no", call)
                self.assertEqual(set(self.containers()), {"dev-current", "turn-current"})
                self.assertTrue(
                    all(item["restart"] == "no" for item in self.containers().values())
                )

    def test_starts_stopped_relay_when_matching_dev_is_running(self) -> None:
        for restart in ("no", "unless-stopped"):
            with self.subTest(restart=restart):
                self.set_containers(
                    {
                        "dev-current": self.container("opk-dev", CURRENT_IP, restart=restart),
                        "turn-current": self.container(
                            "coturn", CURRENT_IP, running=False, restart=restart
                        ),
                    }
                )
                completed = self.run_init()
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assert_removed()
                self.assertEqual(self.invocations("start"), [["start", "turn-current"]])
                containers = self.containers()
                self.assertEqual(set(containers), {"dev-current", "turn-current"})
                self.assertTrue(all(item["running"] for item in containers.values()))
                self.assertTrue(all(item["restart"] == "no" for item in containers.values()))

    def test_stopped_dev_leaves_relay_startup_to_compose(self) -> None:
        for turn_running in (True, False):
            with self.subTest(turn_running=turn_running):
                containers = {
                    "dev-current": self.container(
                        "opk-dev", CURRENT_IP, running=False, restart="no"
                    ),
                    "turn-current": self.container(
                        "coturn", CURRENT_IP, running=turn_running, restart="no"
                    ),
                }
                self.set_containers(containers)
                completed = self.run_init()
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assertEqual(self.invocations("start"), [])
                self.assert_removed()
                self.assertEqual(self.containers(), containers)

    def test_running_state_inspection_and_relay_start_failures_are_fatal(self) -> None:
        for failure in ("inspect-running-dev-current", "inspect-running-turn-current", "start"):
            with self.subTest(failure=failure):
                containers = {
                    "dev-current": self.container("opk-dev", CURRENT_IP, restart="no"),
                    "turn-current": self.container(
                        "coturn", CURRENT_IP, running=False, restart="no"
                    ),
                }
                self.set_containers(containers, failure=failure)
                completed = self.run_init()
                self.assertNotEqual(completed.returncode, 0, completed.stdout)
                self.assertIn(f"Simulated Docker {failure} failure", completed.stderr)
                self.assert_removed()
                self.assertEqual(self.containers(), containers)

    def test_other_projects_working_directories_and_services_are_untouched(self) -> None:
        other_directory = self.temporary_root / "other checkout"
        other_directory.mkdir()
        cases = {
            "wrong-working-directory": {
                "dev-other": self.container("opk-dev", working_dir=other_directory),
                "turn-other": self.container("coturn", working_dir=other_directory),
            },
            "other-project": {
                "dev-other": self.container("opk-dev", project="other-project"),
                "turn-other": self.container("coturn", project="other-project"),
            },
            "rich": {
                "dev-rich": self.container(
                    "opk-dev-rich", compose_file="docker-compose.rich.yaml"
                ),
                "turn-rich": self.container(
                    "coturn", compose_file="docker-compose.rich.yaml"
                ),
            },
            "other-service": {"unrelated": self.container("unrelated")},
        }
        for name, containers in cases.items():
            with self.subTest(name=name):
                self.set_containers(containers)
                completed = self.run_init()
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assertEqual(self.containers(), containers)
                self.assert_removed()
                self.assertEqual(self.invocations("update"), [])
                self.assertEqual(self.invocations("stop"), [])
                self.assertEqual(self.invocations("start"), [])

    def test_restart_override_and_reconciliation_only_apply_to_vscode_turn_launch(self) -> None:
        for arguments, expected in (
            ({}, {"opk-dev": "no", "coturn": "no"}),
            ({"turn": "auto"}, {"opk-dev": "no", "coturn": "no"}),
            ({"turn": "disabled"}, {}),
            ({"cleanup": False}, {}),
            ({"service": "opk-dev-rich"}, {}),
            ({"service": "opk-dev-rich", "kind": "rich"}, {}),
        ):
            with self.subTest(arguments=arguments):
                workspace = self.checkout / ".devcontainer/docker-compose.devcont.workspace.yaml"
                workspace.unlink(missing_ok=True)
                containers = {
                    "dev-current": self.container("opk-dev"),
                    "turn-current": self.container("coturn"),
                }
                self.set_containers(containers)
                completed = self.run_init(**arguments)
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assertEqual(self.workspace_restart_policies(), expected)
                if not expected:
                    self.assertEqual(self.containers(), containers)
                    self.assert_removed()
                    self.assertEqual(self.invocations("update"), [])
                    self.assertEqual(self.invocations("start"), [])

    def test_explicit_ip_is_used_for_reconciliation_and_env_without_detection(self) -> None:
        explicit_ip = "198.51.100.7"
        self.set_containers(
            {
                "dev-current": self.container("opk-dev", explicit_ip, restart="no"),
                "turn-current": self.container("coturn", explicit_ip, restart="no"),
            }
        )
        completed = self.run_init(explicit_ip=explicit_ip)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assert_removed()
        self.assertEqual(self.invocations("update"), [])
        self.assertFalse(self.detector_log.exists())
        for name in (".env", ".devcontainer/.env", "devices.env"):
            self.assertIn(
                f"WEBRTC_HOST_IP={explicit_ip}\n",
                (self.checkout / name).read_text(encoding="utf-8"),
            )

    def test_docker_discovery_inspection_removal_and_update_failures_are_fatal(self) -> None:
        for command in ("ps", "inspect", "rm", "update"):
            with self.subTest(command=command):
                ip = CURRENT_IP if command == "update" else STALE_IP
                self.set_containers(
                    {
                        "dev-current": self.container("opk-dev", ip),
                        "turn-current": self.container("coturn", ip),
                    },
                    failure=command,
                )
                completed = self.run_init()
                self.assertNotEqual(completed.returncode, 0, completed.stdout)
                self.assertIn(f"Simulated Docker {command} failure", completed.stderr)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
