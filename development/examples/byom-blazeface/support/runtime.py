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

"""Environment discovery and reliable child-process shutdown."""

from __future__ import annotations

from contextlib import contextmanager
from dataclasses import dataclass
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time
from typing import Any


REEXEC_MARKER = "BYOM_BLAZEFACE_REEXEC"
POLL_INTERVAL_SECONDS = 0.05


class ExampleError(RuntimeError):
    """A user-facing failure in the BYOM example."""


class ExampleInterrupted(RuntimeError):
    """The user requested that the example stop."""


class ShutdownState:
    """Coordinate signals with child processes that need graceful teardown."""

    def __init__(self) -> None:
        self.requested = False
        self._managed_process_depth = 0

    def request(self, _signum: int, _frame: Any) -> None:
        self.requested = True
        if self._managed_process_depth == 0:
            raise ExampleInterrupted

    def check(self) -> None:
        if self.requested:
            raise ExampleInterrupted

    def begin_managed_process(self) -> None:
        self.check()
        self._managed_process_depth += 1

    def end_managed_process(self) -> None:
        self._managed_process_depth -= 1


@contextmanager
def shutdown_signal_handlers(shutdown: ShutdownState):
    previous_handlers = {
        signum: signal.getsignal(signum) for signum in (signal.SIGINT, signal.SIGTERM)
    }
    for signum in previous_handlers:
        signal.signal(signum, shutdown.request)
    try:
        yield
    finally:
        for signum, handler in previous_handlers.items():
            signal.signal(signum, handler)


@dataclass(frozen=True)
class ExamplePaths:
    example_dir: Path
    repository_root: Path
    opk_menu: Path
    source_video: Path
    pipeline: Path
    model: Path
    output_video: Path


@dataclass(frozen=True)
class OptionalTools:
    ffmpeg: Path | None
    ffprobe: Path | None


def ensure_sdk_python(script_path: Path, shutdown: ShutdownState) -> None:
    """Re-execute once with a supported open-perception-kit interpreter if needed."""

    runtime_interpreter = _python_ops_runtime()
    shutdown.check()
    current_interpreter = Path(sys.executable)
    if _current_python_has_perception_sdk():
        return

    if os.environ.get(REEXEC_MARKER) == "1":
        raise ExampleError(
            "the selected Python interpreter does not provide the open-perception-kit: "
            f"{current_interpreter}"
        )

    candidates = []
    devtools_venv = os.environ.get("OPK_DEVTOOLS_VENV")
    if devtools_venv:
        candidates.append(Path(devtools_venv).expanduser().resolve() / "bin/python")
    candidates.append(runtime_interpreter)

    interpreter = next(
        (
            candidate
            for candidate in dict.fromkeys(candidates)
            if candidate.is_file()
            and os.access(candidate, os.X_OK)
            and interpreter_has_perception_sdk(candidate)
        ),
        None,
    )
    if interpreter is None:
        raise ExampleError(
            "no supported Python interpreter provides the standalone open-perception-kit; "
            "use an official OPK development container or binary release"
        )

    shutdown.check()
    environment = os.environ.copy()
    environment[REEXEC_MARKER] = "1"
    try:
        os.execve(
            str(interpreter),
            [str(interpreter), str(script_path), *sys.argv[1:]],
            environment,
        )
    except OSError as exc:
        raise ExampleError(f"failed to start supported Python interpreter: {exc}") from exc


def discover_example(script_path: Path) -> tuple[ExamplePaths, OptionalTools]:
    """Resolve and validate every file used by the example."""

    example_dir = script_path.resolve().parent
    repository_root = _repository_root(example_dir)
    required_example_files = (
        ".gitignore",
        "README.md",
        "model.json",
        "opchain.json",
        "pipeline.json",
        "postprocess.py",
        "run.py",
    )
    missing = [name for name in required_example_files if not (example_dir / name).is_file()]
    if missing:
        raise ExampleError(f"example is incomplete; missing: {', '.join(missing)}")

    opk_menu = repository_root / "tools/opk-menu"
    source_video = repository_root / "data/videos/GettyImages-1129703310.mov"
    if not os.access(opk_menu, os.X_OK):
        raise ExampleError(f"OPK launcher is not executable: {opk_menu}")
    if not source_video.is_file():
        raise ExampleError(f"required prerecorded video is missing: {source_video}")

    paths = ExamplePaths(
        example_dir=example_dir,
        repository_root=repository_root,
        opk_menu=opk_menu,
        source_video=source_video,
        pipeline=example_dir / "pipeline.json",
        model=example_dir / "face_detector.onnx",
        output_video=example_dir / "blazeface-detections.mp4",
    )
    tools = OptionalTools(
        ffmpeg=_optional_executable("ffmpeg"),
        ffprobe=_optional_executable("ffprobe"),
    )
    return paths, tools


def pipeline_environment(paths: ExamplePaths, results_path: Path) -> dict[str, str]:
    environment = os.environ.copy()
    environment.pop("HF_TOKEN", None)
    environment.update(
        {
            "OPK_PROJECT_ROOT": str(paths.repository_root),
            "BYOM_EXAMPLE_DIR": str(paths.example_dir),
            "BYOM_RESULTS": str(results_path),
        }
    )
    return environment


class ManagedProcess:
    """Run one process group with SIGINT, SIGTERM, then SIGKILL escalation."""

    def __init__(
        self,
        command: list[str],
        shutdown: ShutdownState,
        label: str,
        *,
        environment: dict[str, str] | None = None,
        stdout: Any = None,
        stderr: Any = None,
    ) -> None:
        self._command = command
        self._shutdown = shutdown
        self._label = label
        self._environment = environment
        self._stdout = stdout
        self._stderr = stderr
        self._child: subprocess.Popen[Any] | None = None
        self._stop_requested = False
        self._sent_signal: signal.Signals | None = None
        self._deadline: float | None = None

    def __enter__(self) -> "ManagedProcess":
        self._shutdown.begin_managed_process()
        try:
            self._child = subprocess.Popen(
                self._command,
                env=self._environment,
                stdin=subprocess.DEVNULL,
                stdout=self._stdout,
                stderr=self._stderr,
                start_new_session=True,
            )
        except OSError as exc:
            self._shutdown.end_managed_process()
            if self._shutdown.requested:
                raise ExampleInterrupted from exc
            raise ExampleError(f"failed to start {self._label}: {exc}") from exc
        return self

    def __exit__(self, _type: Any, _value: Any, _traceback: Any) -> None:
        try:
            if self._child is not None:
                if self.running:
                    send_process_signal(self._child, signal.SIGKILL)
                self._child.wait()
        finally:
            self._shutdown.end_managed_process()

    @property
    def running(self) -> bool:
        return self._child is not None and self._child.poll() is None

    @property
    def returncode(self) -> int:
        if self._child is None or self._child.returncode is None:
            raise ExampleError(f"{self._label} exited without a status")
        return self._child.returncode

    def request_stop(self) -> None:
        self._stop_requested = True

    def update_shutdown(self) -> None:
        if self._child is None:
            return
        now = time.monotonic()
        if (self._shutdown.requested or self._stop_requested) and self._sent_signal is None:
            send_process_signal(self._child, signal.SIGINT)
            self._sent_signal = signal.SIGINT
            self._deadline = now + 10.0
        elif self._deadline is not None and now >= self._deadline:
            if self._sent_signal == signal.SIGINT:
                send_process_signal(self._child, signal.SIGTERM)
                self._sent_signal = signal.SIGTERM
                self._deadline = now + 5.0
            elif self._sent_signal == signal.SIGTERM:
                send_process_signal(self._child, signal.SIGKILL)
                self._sent_signal = signal.SIGKILL
                self._deadline = None


def run_managed_command(
    command: list[str],
    shutdown: ShutdownState,
    label: str,
    *,
    stdout: Any = None,
    stderr: Any = None,
) -> int:
    with ManagedProcess(
        command,
        shutdown,
        label,
        stdout=stdout,
        stderr=stderr,
    ) as process:
        while process.running:
            process.update_shutdown()
            time.sleep(POLL_INTERVAL_SECONDS)
        child_status = process.returncode
    return 130 if shutdown.requested else child_status


def send_process_signal(
    child: subprocess.Popen[Any], requested_signal: signal.Signals
) -> None:
    if child.poll() is not None:
        return
    try:
        # ManagedProcess creates this child as a new session leader, so its PID
        # identifies only the private process group owned by this runner.
        os.killpg(child.pid, requested_signal)  # NOSONAR
    except ProcessLookupError:
        pass


def _repository_root(example_dir: Path) -> Path:
    configured = os.environ.get("OPK_PROJECT_ROOT")
    candidates = [Path(configured).expanduser()] if configured else []
    candidates.extend(example_dir.parents)
    for candidate in candidates:
        root = candidate.resolve()
        if (root / "tools/opk-menu").is_file() and (root / "data/videos").is_dir():
            return root
    raise ExampleError(
        "cannot locate the OPK repository root; set OPK_PROJECT_ROOT to the checkout root"
    )


def _python_ops_runtime() -> Path:
    venv_value = os.environ.get("OPK_PYTHON_RUNTIME_VENV")
    if not venv_value:
        raise ExampleError(
            "OPK_PYTHON_RUNTIME_VENV is not set; run this example inside an official "
            "OPK container or supported binary-release environment with Python Ops enabled"
        )
    interpreter = Path(venv_value).expanduser().resolve() / "bin/python"
    if not interpreter.is_file() or not os.access(interpreter, os.X_OK):
        raise ExampleError(f"supported OPK Python runtime is unavailable at {interpreter}")
    return interpreter


def _current_python_has_perception_sdk() -> bool:
    try:
        from open_perception_kit import ProducerIdentityStatus
        from open_perception_kit.packet import decode, external_key
    except ImportError:
        return False
    return (
        ProducerIdentityStatus is not None
        and decode is not None
        and external_key is not None
    )


def interpreter_has_perception_sdk(interpreter: Path) -> bool:
    if interpreter.absolute() == Path(sys.executable).absolute():
        return _current_python_has_perception_sdk()
    try:
        result = subprocess.run(
            [
                str(interpreter),
                "-c",
                "from open_perception_kit import ProducerIdentityStatus; "
                "from open_perception_kit.packet import decode, external_key",
            ],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=10,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        return False
    return result.returncode == 0


def _optional_executable(name: str) -> Path | None:
    executable = shutil.which(name)
    return Path(executable).resolve() if executable is not None else None
