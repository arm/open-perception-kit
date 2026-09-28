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

from __future__ import annotations
import open_perception_kit.sdk as perception_sdk
from open_perception_kit.packet import external_key
from open_perception_kit.packet import Envelope as PacketEnvelope
from open_perception_kit import ProducerIdentityStatus

import base64
import hashlib
import importlib
import importlib.util
import io
import json
import os
from pathlib import Path
import signal
import sys
import tempfile
import types
import unittest
from unittest import mock

import numpy


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
EXAMPLE_DIRECTORY = REPOSITORY_ROOT / "development/examples/byom-blazeface"
sys.path.insert(0, str(EXAMPLE_DIRECTORY))


def _load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _python_script(callback):
    return callback


guest_module = types.ModuleType("open_perception_kit.guest")
guest_module.Envelope = object
guest_module.external_key = lambda name: name
sys.modules["open_perception_kit.guest"] = guest_module

python_ops_module = types.ModuleType("opk_python_ops")
python_ops_module.Context = object
python_ops_module.Tensor = object
python_ops_module.python_script = _python_script
sys.modules["opk_python_ops"] = python_ops_module

postprocess = _load_module("byom_blazeface_postprocess", EXAMPLE_DIRECTORY / "postprocess.py")
runner = _load_module("byom_blazeface_runner", EXAMPLE_DIRECTORY / "run.py")
model_support = importlib.import_module("support.model")
pipeline_support = importlib.import_module("support.pipeline")
result_support = importlib.import_module("support.results")
runtime_support = importlib.import_module("support.runtime")
video_support = importlib.import_module("support.video")


class FakeTensor:
    def __init__(self, name: str | None, values: numpy.ndarray):
        self.name = name
        self.array = values
        self.quantized = False


class FakeEnvelope:
    def __init__(self):
        self.payloads = []

    def add(self, key, payload):
        self.payloads.append((key, payload))


class RunningChild:
    def __init__(self):
        self.pid = 1234
        self.waited = False

    def poll(self):
        return None

    def wait(self):
        self.waited = True
        return 0


class ByomBlazeFaceExampleTest(unittest.TestCase):
    def test_postprocessor_validates_outputs_and_emits_compact_json(self):
        boxes = numpy.zeros(postprocess.BOX_SHAPE, dtype=numpy.float32)
        scores = numpy.full(postprocess.SCORE_SHAPE, -100.0, dtype=numpy.float32)
        boxes[0, 400, :4] = (0.0, 0.0, 25.6, 25.6)
        scores[0, 400, 0] = 10.0
        envelope = FakeEnvelope()

        postprocess.process(
            envelope,
            (FakeTensor("classificators", scores), FakeTensor("regressors", boxes)),
            object(),
        )

        self.assertEqual(len(envelope.payloads), 1)
        key, payload = envelope.payloads[0]
        self.assertEqual(key, postprocess.FACES_KEY)
        self.assertNotIn(b" ", payload)
        faces = json.loads(payload)["faces"]
        self.assertEqual(len(faces), 1)
        anchor = postprocess.ANCHORS[400]
        self.assertAlmostEqual(faces[0]["x"], max(float(anchor[0]) - 0.1, 0.0))
        self.assertAlmostEqual(faces[0]["y"], max(float(anchor[1]) - 0.1, 0.0))
        self.assertAlmostEqual(faces[0]["width"], 0.2)
        self.assertAlmostEqual(faces[0]["height"], 0.2)
        self.assertAlmostEqual(faces[0]["confidence"], 1.0 / (1.0 + numpy.exp(-10.0)))

    def test_postprocessor_supports_unambiguous_unnamed_outputs(self):
        boxes = numpy.zeros(postprocess.BOX_SHAPE, dtype=numpy.float32)
        scores = numpy.zeros(postprocess.SCORE_SHAPE, dtype=numpy.float32)

        actual_boxes, actual_scores = postprocess._validated_outputs(
            (FakeTensor(None, scores), FakeTensor(None, boxes))
        )

        self.assertIs(actual_boxes, boxes)
        self.assertIs(actual_scores, scores)

    def test_runner_orchestrates_the_complete_example_in_order(self):
        paths = runtime_support.ExamplePaths(
            example_dir=EXAMPLE_DIRECTORY,
            repository_root=REPOSITORY_ROOT,
            opk_menu=REPOSITORY_ROOT / "tools/opk-menu",
            source_video=REPOSITORY_ROOT / "data/videos/GettyImages-1129703310.mov",
            pipeline=EXAMPLE_DIRECTORY / "pipeline.json",
            model=EXAMPLE_DIRECTORY / "face_detector.onnx",
            output_video=EXAMPLE_DIRECTORY / "blazeface-detections.mp4",
        )
        tools = runtime_support.OptionalTools(
            ffmpeg=Path("/usr/bin/ffmpeg"),
            ffprobe=Path("/usr/bin/ffprobe"),
        )
        shutdown = runtime_support.ShutdownState()
        calls = []

        with (
            mock.patch.object(
                runtime_support,
                "discover_example",
                side_effect=lambda _script: calls.append("discover") or (paths, tools),
            ),
            mock.patch.object(
                model_support,
                "ensure_model",
                side_effect=lambda *_args: calls.append("model"),
            ) as ensure_model,
            mock.patch.object(
                pipeline_support,
                "run_pipeline",
                side_effect=lambda *_args: calls.append("pipeline") or (0, {}),
            ) as pipeline,
            mock.patch.object(
                video_support,
                "render_video",
                side_effect=lambda *_args: calls.append("video") or 0,
            ) as render,
        ):
            status = runner.run_example(EXAMPLE_DIRECTORY / "run.py", shutdown)

        self.assertEqual(status, 0)
        self.assertEqual(calls, ["discover", "model", "pipeline", "video"])
        ensure_model.assert_called_once_with(paths.model, paths.repository_root, shutdown)
        pipeline.assert_called_once()
        render.assert_called_once()

    def test_runner_returns_pipeline_failure_without_rendering(self):
        paths, tools = self._example_runtime()
        shutdown = runtime_support.ShutdownState()
        with (
            mock.patch.object(runtime_support, "discover_example", return_value=(paths, tools)),
            mock.patch.object(model_support, "ensure_model"),
            mock.patch.object(pipeline_support, "run_pipeline", return_value=(7, {})),
            mock.patch.object(video_support, "render_video") as render,
        ):
            status = runner.run_example(EXAMPLE_DIRECTORY / "run.py", shutdown)

        self.assertEqual(status, 7)
        render.assert_not_called()

    def test_runner_succeeds_without_optional_ffmpeg_tools(self):
        paths, _tools = self._example_runtime()
        shutdown = runtime_support.ShutdownState()
        unavailable_tools = runtime_support.OptionalTools(ffmpeg=None, ffprobe=None)
        with (
            mock.patch.object(
                runtime_support,
                "discover_example",
                return_value=(paths, unavailable_tools),
            ),
            mock.patch.object(model_support, "ensure_model"),
            mock.patch.object(pipeline_support, "run_pipeline", return_value=(0, {})),
            mock.patch.object(video_support, "render_video") as render,
            mock.patch("sys.stderr", new_callable=io.StringIO),
        ):
            status = runner.run_example(EXAMPLE_DIRECTORY / "run.py", shutdown)

        self.assertEqual(status, 0)
        render.assert_not_called()

    def test_pipeline_environment_does_not_expose_hugging_face_token(self):
        paths, _tools = self._example_runtime()
        results_path = Path("/tmp/frame-results.ndjson")
        with mock.patch.dict(
            os.environ,
            {"HF_TOKEN": "sensitive", "PRESERVED_VARIABLE": "value"},
            clear=True,
        ):
            environment = runtime_support.pipeline_environment(paths, results_path)

        self.assertNotIn("HF_TOKEN", environment)
        self.assertEqual(environment["PRESERVED_VARIABLE"], "value")
        self.assertEqual(environment["OPK_PROJECT_ROOT"], str(paths.repository_root))
        self.assertEqual(environment["BYOM_EXAMPLE_DIR"], str(paths.example_dir))
        self.assertEqual(environment["BYOM_RESULTS"], str(results_path))

    def test_listener_rejects_every_incompatible_producer_identity(self):
        cases = (
            ({"SDK_NAME": "different_sdk"}, ProducerIdentityStatus.SDK_NAME_MISMATCH),
            ({"SDK_VERSION": "9.9.9"}, ProducerIdentityStatus.SDK_VERSION_MISMATCH),
            (
                {"SCHEMA_SET_SHA256": "f" * 64},
                ProducerIdentityStatus.SCHEMA_SET_MISMATCH,
            ),
            ({"SDK_NAME": ""}, ProducerIdentityStatus.MISSING),
            ({"SDK_VERSION": "invalid"}, ProducerIdentityStatus.MALFORMED),
        )
        for overrides, expected_status in cases:
            with self.subTest(status=expected_status.value):
                packet = self._packet_with_identity(overrides)
                with self.assertRaisesRegex(
                    runtime_support.ExampleError,
                    f"frame 17: incompatible FrameResults producer identity: "
                    f"{expected_status.value}",
                ):
                    result_support.FaceResultDecoder().decode_record(
                        self._record(17, packet), 1
                    )

    def test_listener_accepts_exact_identity_and_payload(self):
        packet = self._packet_with_identity({})

        with mock.patch("sys.stdout", new_callable=io.StringIO) as output:
            result = result_support.FaceResultDecoder().decode_record(
                self._record(18, packet), 1
            )
            result_support.print_frame(result)

        self.assertEqual(result, result_support.FrameFaces(frame=18, faces=()))
        self.assertEqual(output.getvalue(), "frame 18: 0 faces\n")

    def test_failed_process_launches_preserve_signal_handlers(self):
        original_handlers = {
            signum: signal.getsignal(signum)
            for signum in (signal.SIGINT, signal.SIGTERM)
        }
        shutdown = runtime_support.ShutdownState()
        with runtime_support.shutdown_signal_handlers(shutdown):
            with mock.patch.object(
                runtime_support.subprocess,
                "Popen",
                side_effect=OSError("not executable"),
            ):
                with tempfile.NamedTemporaryFile() as results:
                    with self.assertRaisesRegex(runtime_support.ExampleError, "OPK pipeline"):
                        pipeline_support.run_pipeline(
                            Path("/missing/opk-menu"),
                            Path("/missing/pipeline.json"),
                            os.environ.copy(),
                            Path(results.name),
                            shutdown,
                        )
                with self.assertRaisesRegex(runtime_support.ExampleError, "ffmpeg"):
                    runtime_support.run_managed_command(
                        ["/missing/ffmpeg"], shutdown, "ffmpeg"
                    )

        self.assertEqual(
            {
                signum: signal.getsignal(signum)
                for signum in (signal.SIGINT, signal.SIGTERM)
            },
            original_handlers,
        )

    def test_listener_setup_failure_reaps_started_pipeline(self):
        child = RunningChild()
        with (
            mock.patch.object(runtime_support.subprocess, "Popen", return_value=child),
            mock.patch.object(
                pipeline_support,
                "NdjsonFollower",
                side_effect=OSError("cannot open output"),
            ),
            mock.patch.object(runtime_support, "send_process_signal") as signal_group,
            self.assertRaisesRegex(runtime_support.ExampleError, "monitor OPK pipeline"),
        ):
            pipeline_support.run_pipeline(
                Path("/opk-menu"),
                Path("/pipeline.json"),
                os.environ.copy(),
                Path("/results.ndjson"),
                runtime_support.ShutdownState(),
            )

        signal_group.assert_called_once_with(child, signal.SIGKILL)
        self.assertTrue(child.waited)

    def test_signal_during_pipeline_launch_uses_graceful_shutdown(self):
        shutdown = runtime_support.ShutdownState()
        child = mock.Mock(pid=1234, returncode=0)
        child.poll.side_effect = (None, 0, 0)

        def start_child(*_args, **_kwargs):
            shutdown.request(signal.SIGTERM, None)
            return child

        with tempfile.NamedTemporaryFile() as results:
            with (
                mock.patch.object(runtime_support.subprocess, "Popen", side_effect=start_child),
                mock.patch.object(runtime_support, "send_process_signal") as signal_group,
                mock.patch.object(pipeline_support.time, "sleep"),
            ):
                status, frames = pipeline_support.run_pipeline(
                    Path("/opk-menu"),
                    Path("/pipeline.json"),
                    os.environ.copy(),
                    Path(results.name),
                    shutdown,
                )

        self.assertEqual(status, 130)
        self.assertEqual(frames, {})
        signal_group.assert_called_once_with(child, signal.SIGINT)
        child.wait.assert_called_once()

    def test_signal_during_pipeline_cleanup_returns_interrupted_status(self):
        shutdown = runtime_support.ShutdownState()
        child = mock.Mock(pid=1234, returncode=0)
        child.poll.return_value = 0
        child.wait.side_effect = lambda: shutdown.request(signal.SIGTERM, None)

        with tempfile.NamedTemporaryFile() as results:
            with mock.patch.object(runtime_support.subprocess, "Popen", return_value=child):
                status, frames = pipeline_support.run_pipeline(
                    Path("/opk-menu"),
                    Path("/pipeline.json"),
                    os.environ.copy(),
                    Path(results.name),
                    shutdown,
                )

        self.assertEqual(status, 130)
        self.assertEqual(frames, {})

    def test_signal_during_ffmpeg_cleanup_returns_interrupted_status(self):
        shutdown = runtime_support.ShutdownState()
        child = mock.Mock(pid=1234, returncode=0)
        child.poll.return_value = 0
        child.wait.side_effect = lambda: shutdown.request(signal.SIGTERM, None)

        with mock.patch.object(runtime_support.subprocess, "Popen", return_value=child):
            status = runtime_support.run_managed_command(["ffmpeg"], shutdown, "ffmpeg")

        self.assertEqual(status, 130)

    def test_process_signal_targets_the_managed_child_group(self):
        child = RunningChild()

        with mock.patch.object(runtime_support.os, "killpg") as kill_process_group:
            runtime_support.send_process_signal(child, signal.SIGINT)

        kill_process_group.assert_called_once_with(child.pid, signal.SIGINT)

    def test_sdk_interpreter_selection_skips_incompatible_candidate(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            devtools = root / "devtools/bin/python"
            runtime = root / "runtime/bin/python"
            for interpreter in (devtools, runtime):
                interpreter.parent.mkdir(parents=True)
                interpreter.touch(mode=0o700)

            environment = {
                "OPK_DEVTOOLS_VENV": str(devtools.parents[1]),
                "OPK_PYTHON_RUNTIME_VENV": str(runtime.parents[1]),
            }
            with (
                mock.patch.dict(os.environ, environment, clear=True),
                mock.patch.object(
                    runtime_support, "_current_python_has_perception_sdk", return_value=False
                ),
                mock.patch.object(
                    runtime_support,
                    "interpreter_has_perception_sdk",
                    side_effect=lambda candidate: candidate == runtime,
                ),
                mock.patch.object(os, "execve", side_effect=RuntimeError("reexec")) as execve,
                self.assertRaisesRegex(RuntimeError, "reexec"),
            ):
                runtime_support.ensure_sdk_python(
                    EXAMPLE_DIRECTORY / "run.py",
                    runtime_support.ShutdownState(),
                )

            self.assertEqual(Path(execve.call_args.args[0]), runtime)

    def test_venv_symlink_is_probed_as_a_distinct_environment(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            interpreter = Path(temporary_directory) / "bin/python"
            interpreter.parent.mkdir()
            interpreter.symlink_to(sys.executable)
            completed = runtime_support.subprocess.CompletedProcess(
                [str(interpreter)],
                returncode=0,
            )

            with (
                mock.patch.object(
                    runtime_support,
                    "_current_python_has_perception_sdk",
                    return_value=False,
                ),
                mock.patch.object(runtime_support.subprocess, "run", return_value=completed) as run,
            ):
                self.assertTrue(runtime_support.interpreter_has_perception_sdk(interpreter))

            run.assert_called_once()

    def test_verified_model_reuses_cache_without_downloader(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            model = root / "face_detector.onnx"
            model.write_bytes(b"verified-model")
            self._write_model_descriptor(root, b"verified-model")

            with mock.patch.object(model_support, "run_managed_command") as download:
                model_support.ensure_model(
                    model,
                    REPOSITORY_ROOT,
                    runtime_support.ShutdownState(),
                )

            download.assert_not_called()

    def test_corrupt_model_is_replaced_through_download_owner(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            scripts = root / "scripts"
            scripts.mkdir()
            downloader = scripts / "download-models.py"
            downloader.touch(mode=0o700)
            model = root / "example/face_detector.onnx"
            model.parent.mkdir()
            model.write_bytes(b"corrupt")
            self._write_model_descriptor(model.parent, b"verified-model")

            def install(*_args, **_kwargs):
                model.write_bytes(b"verified-model")
                return 0

            with mock.patch.object(
                model_support,
                "run_managed_command",
                side_effect=install,
            ) as download, mock.patch.object(
                model_support,
                "_model_downloader_python",
                return_value="/devtools/bin/python",
            ):
                model_support.ensure_model(
                    model,
                    root,
                    runtime_support.ShutdownState(),
                )

            download.assert_called_once_with(
                [
                    "/devtools/bin/python",
                    str(downloader),
                    "--models-dir",
                    str(model.parent),
                ],
                mock.ANY,
                "model downloader",
            )
            self.assertEqual(model.read_bytes(), b"verified-model")

    def test_model_download_owner_interruption_is_propagated(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            scripts = root / "scripts"
            scripts.mkdir()
            (scripts / "download-models.py").touch(mode=0o700)
            model = root / "example/face_detector.onnx"
            model.parent.mkdir()
            self._write_model_descriptor(model.parent, b"verified-model")

            with (
                mock.patch.object(model_support, "run_managed_command", return_value=130),
                mock.patch.object(
                    model_support,
                    "_model_downloader_python",
                    return_value="/devtools/bin/python",
                ),
                self.assertRaises(runtime_support.ExampleInterrupted),
            ):
                model_support.ensure_model(
                    model,
                    root,
                    runtime_support.ShutdownState(),
                )

    @staticmethod
    def _write_model_descriptor(directory: Path, model_bytes: bytes) -> None:
        (directory / "model.json").write_text(
            json.dumps(
                {
                    "hfDownload": {
                        "sha256": hashlib.sha256(model_bytes).hexdigest(),
                    }
                }
            )
        )

    @staticmethod
    def _example_runtime():
        paths = runtime_support.ExamplePaths(
            example_dir=EXAMPLE_DIRECTORY,
            repository_root=REPOSITORY_ROOT,
            opk_menu=REPOSITORY_ROOT / "tools/opk-menu",
            source_video=REPOSITORY_ROOT / "data/videos/GettyImages-1129703310.mov",
            pipeline=EXAMPLE_DIRECTORY / "pipeline.json",
            model=EXAMPLE_DIRECTORY / "face_detector.onnx",
            output_video=EXAMPLE_DIRECTORY / "blazeface-detections.mp4",
        )
        tools = runtime_support.OptionalTools(
            ffmpeg=Path("/usr/bin/ffmpeg"),
            ffprobe=Path("/usr/bin/ffprobe"),
        )
        return paths, tools

    @staticmethod
    def _packet_with_identity(overrides: dict[str, str]) -> bytes:
        originals = {
            name: getattr(perception_sdk, name)
            for name in ("SDK_NAME", "SDK_VERSION", "SCHEMA_SET_SHA256")
        }
        try:
            for name, value in overrides.items():
                setattr(perception_sdk, name, value)
            envelope = PacketEnvelope()
            envelope.add(
                external_key(result_support.FACES_KEY_NAME),
                b'{"faces":[]}',
            )
            return envelope.serialize()
        finally:
            for name, value in originals.items():
                setattr(perception_sdk, name, value)

    @staticmethod
    def _record(frame: int, packet: bytes) -> bytes:
        return json.dumps(
            {
                "frame_counter": frame,
                "frame_results_encoding": result_support.FRAME_RESULTS_ENCODING,
                "frame_results_packet_b64": base64.b64encode(packet).decode("ascii"),
            }
        ).encode("utf-8")


if __name__ == "__main__":
    unittest.main()
