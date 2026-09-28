#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

"""Run the complete BlazeFace BYOM example."""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile

from support import model
from support import runtime


def run_example(script_path: Path, shutdown: runtime.ShutdownState) -> int:
    """Verify the model, run OPK, consume results, and render detections."""

    # These modules use the standalone open-perception-kit, so import them only after
    # runtime.ensure_sdk_python() has selected a compatible interpreter.
    from support import pipeline
    from support import video

    paths, tools = runtime.discover_example(script_path)
    model.ensure_model(paths.model, paths.repository_root, shutdown)

    with tempfile.TemporaryDirectory(prefix="opk-byom-blazeface-") as temporary_dir:
        temporary_path = Path(temporary_dir)
        results_path = temporary_path / "frame-results.ndjson"
        results_path.touch(mode=0o600)

        pipeline_status, frames = pipeline.run_pipeline(
            paths.opk_menu,
            paths.pipeline,
            runtime.pipeline_environment(paths, results_path),
            results_path,
            shutdown,
        )
        if pipeline_status != 0:
            return pipeline_status

        if tools.ffmpeg is None or tools.ffprobe is None:
            print(
                "FFmpeg tools are unavailable; skipping optional annotated-video rendering.",
                file=sys.stderr,
            )
            return 0
        return video.render_video(
            tools.ffmpeg,
            tools.ffprobe,
            paths.source_video,
            paths.output_video,
            temporary_path / "drawbox-filter.txt",
            frames,
            shutdown,
        )


def main() -> int:
    script_path = Path(__file__).resolve()
    shutdown = runtime.ShutdownState()
    try:
        with runtime.shutdown_signal_handlers(shutdown):
            runtime.ensure_sdk_python(script_path, shutdown)
            return run_example(script_path, shutdown)
    except (KeyboardInterrupt, runtime.ExampleInterrupted):
        return 130
    except runtime.ExampleError as exc:
        print(f"BYOM BlazeFace error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
