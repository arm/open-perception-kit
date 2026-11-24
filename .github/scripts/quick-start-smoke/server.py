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

"""Keep the smoke pipeline alive until Playwright closes its owned stdin pipe."""

import os
import signal
import subprocess
import sys
import threading


def run_pipeline() -> int:
    with subprocess.Popen(
        ["./tools/opk-menu", ".github/scripts/quick-start-smoke/pipeline.json"],
        stdin=subprocess.DEVNULL,
    ) as pipeline:
        def stop_when_disconnected():
            while os.read(sys.stdin.fileno(), 4096):
                pass
            pipeline.send_signal(signal.SIGINT)
            try:
                pipeline.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pipeline.kill()

        threading.Thread(target=stop_when_disconnected, daemon=True).start()
        return pipeline.wait()


def main() -> int:
    if sys.argv[1:] == ["--pipeline"]:
        return run_pipeline()
    # docker exec does not forward host signals. EOF is the shutdown signal to
    # the container supervisor, including when the host runner exits unexpectedly.
    with subprocess.Popen(
        ["docker", "exec", "-i", "--user", "dev", "--workdir", "/work",
         "open-perception-kit", "python3", ".github/scripts/quick-start-smoke/server.py",
         "--pipeline"],
        stdin=subprocess.PIPE,
        start_new_session=True,
    ) as worker:
        signal.signal(signal.SIGTERM, lambda *_: worker.stdin.close())
        signal.signal(signal.SIGINT, lambda *_: worker.stdin.close())
        return worker.wait()


if __name__ == "__main__":
    raise SystemExit(main())
