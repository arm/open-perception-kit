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

"""Run bounded synthetic native replies in isolated namespaces."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time


def stop(process: subprocess.Popen | None) -> None:
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=2)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True)
    parser.add_argument("--client", required=True)
    parser.add_argument("--schema", required=True)
    parser.add_argument("--runtime-root", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="native-", dir=args.runtime_root) as directory:
        root = Path(directory)
        for grow in (False, True):
            stopped = root / f"stopped-{grow}"
            runtime = root / f"runtime-{grow}"
            runtime.mkdir()
            env = dict(os.environ, PROBE_RUNTIME=str(runtime),
                       PROBE_SCHEMA_CPP=args.schema, PROBE_SCHEMA_RUST=args.schema,
                       PROBE_STOPPED_FILE=str(stopped))
            env.pop("PROBE_GROW", None)
            if grow:
                env["PROBE_GROW"] = "1"
            client = None
            with (root / f"server-{grow}.log").open("w+") as log:
                server = subprocess.Popen([args.server], env=env, stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 10
                    while time.monotonic() < deadline:
                        log.seek(0)
                        if "ready" in log.read():
                            break
                        if server.poll() is not None:
                            raise RuntimeError("server exited before readiness")
                        time.sleep(0.01)
                    else:
                        raise TimeoutError("readiness")
                    client = subprocess.Popen([args.client], env=env)
                    if server.wait(timeout=20) != 0:
                        raise RuntimeError("server failed")
                    stopped.touch()
                    if client.wait(timeout=10) != 0:
                        raise RuntimeError("client failed")
                    print("PASS", "PowerOfTwo" if grow else "Static")
                finally:
                    stop(client)
                    stop(server)
                    log.seek(0)
                    print(log.read())


if __name__ == "__main__":
    main()
