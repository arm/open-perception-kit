#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Run the complete BlazeFace BYOM example."""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile

from support import model
from support import runtime


def run_example(script_path: Path, shutdown: runtime.ShutdownState) -> int:
    """Verify the model, run PEK, consume results, and render detections."""

    # These modules use the standalone Perception SDK, so import them only after
    # runtime.ensure_sdk_python() has selected a compatible interpreter.
    from support import pipeline
    from support import video

    paths, tools = runtime.discover_example(script_path)
    model.ensure_model(paths.model, paths.repository_root, shutdown)

    with tempfile.TemporaryDirectory(prefix="pek-byom-blazeface-") as temporary_dir:
        temporary_path = Path(temporary_dir)
        results_path = temporary_path / "frame-results.ndjson"
        results_path.touch(mode=0o600)

        pipeline_status, frames = pipeline.run_pipeline(
            paths.pek_menu,
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
