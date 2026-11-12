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

"""Keep finite test pipelines alive until asynchronous file output is published."""

from pathlib import Path
import signal
import subprocess
import tempfile
import time


def run_file_pipeline(arguments, output, expected_records, *, env, cwd, timeout=60):
    # The fixture's appsink holds EOS while its samples remain queued. Stop the
    # native process only after opkcomm has published the expected complete lines.
    command = ["gst-launch-1.0", "-q", *arguments]
    output = Path(output)
    with tempfile.TemporaryFile(mode="w+t") as stdout, tempfile.TemporaryFile(mode="w+t") as stderr:
        with subprocess.Popen(command, env=env, cwd=cwd, stdout=stdout, stderr=stderr) as process:
            try:
                deadline = time.monotonic() + timeout
                while process.poll() is None and (
                        not output.exists() or output.read_text(encoding="utf-8").count("\n") < expected_records):
                    if time.monotonic() >= deadline:
                        raise TimeoutError(f"Metadata writer did not publish {expected_records} records")
                    time.sleep(.01)
                if process.poll() is None:
                    process.send_signal(signal.SIGINT)
                process.wait(timeout=10)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
            stdout.seek(0)
            stderr.seek(0)
            return subprocess.CompletedProcess(command, process.returncode, stdout.read(), stderr.read())
