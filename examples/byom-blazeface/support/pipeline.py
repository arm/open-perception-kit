################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Run the PEK pipeline while following its pekcomm NDJSON output."""

from __future__ import annotations

from pathlib import Path
import sys
import time

from support.results import Face, FaceResultDecoder, print_frame
from support.runtime import (
    ExampleError,
    ManagedProcess,
    POLL_INTERVAL_SECONDS,
    ShutdownState,
)


class NdjsonFollower:
    def __init__(self, path: Path, decoder: FaceResultDecoder) -> None:
        self._source = path.open("rb")
        self._decoder = decoder
        self._pending = b""
        self._record_number = 0
        self.frames: dict[int, tuple[Face, ...]] = {}

    def drain(self, *, discard: bool = False) -> None:
        data = self._source.read()
        if not data or discard:
            return
        self._pending += data
        lines = self._pending.split(b"\n")
        self._pending = lines.pop()
        for line in lines:
            self._record_number += 1
            if not line:
                raise ExampleError(f"record {self._record_number}: empty NDJSON record")
            result = self._decoder.decode_record(line, self._record_number)
            if result.frame in self.frames:
                raise ExampleError(f"frame {result.frame}: duplicate pekcomm record")
            if self.frames and result.frame <= max(self.frames):
                raise ExampleError(
                    f"frame {result.frame}: pekcomm records are not strictly ordered"
                )
            self.frames[result.frame] = result.faces
            print_frame(result)

    def finish(self) -> None:
        self.drain()
        if self._pending:
            raise ExampleError(
                f"record {self._record_number + 1}: incomplete final NDJSON record"
            )

    def close(self) -> None:
        self._source.close()


def run_pipeline(
    pek_menu: Path,
    pipeline_path: Path,
    environment: dict[str, str],
    results_path: Path,
    shutdown: ShutdownState,
) -> tuple[int, dict[int, tuple[Face, ...]]]:
    """Run inference, decode results as they arrive, and return all frame boxes."""

    follower = None
    listener_error = None
    child_status = None
    frames: dict[int, tuple[Face, ...]] = {}
    try:
        with ManagedProcess(
            [str(pek_menu), str(pipeline_path)],
            shutdown,
            "PEK pipeline",
            environment=environment,
            stdout=sys.stderr,
            stderr=sys.stderr,
        ) as process:
            follower = NdjsonFollower(results_path, FaceResultDecoder())
            while process.running:
                if listener_error is None:
                    try:
                        follower.drain()
                    except ExampleError as exc:
                        listener_error = exc
                        process.request_stop()
                else:
                    follower.drain(discard=True)
                process.update_shutdown()
                time.sleep(POLL_INTERVAL_SECONDS)

            if listener_error is None:
                follower.finish()
            if listener_error is not None:
                raise listener_error
            child_status = process.returncode
            frames = follower.frames
    except OSError as exc:
        raise ExampleError(f"failed to monitor PEK pipeline results: {exc}") from exc
    finally:
        if follower is not None:
            follower.close()

    if child_status is None:
        raise ExampleError("PEK pipeline exited without a status")
    return (130 if shutdown.requested else child_status), frames
